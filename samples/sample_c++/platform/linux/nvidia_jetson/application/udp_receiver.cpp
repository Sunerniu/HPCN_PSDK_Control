/**
 ********************************************************************
 * @file    udp_receiver.cpp
 * @brief   UDP命令接收模块实现 - 接收PC发送的控制命令
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 *********************************************************************
 */

/* Includes ----------------------------------------------------------*/
#include "udp_receiver.hpp"
#include "console_handler.hpp"
#include "command_control.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <dji_logger.h>
#include <dji_platform.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

/* Private constants -------------------------------------------------*/
#define UDP_RECEIVER_THREAD_STACK_SIZE 2048
#define UDP_HEARTBEAT_STOP_TIMEOUT_SEC 10
#define UDP_HEARTBEAT_GOHOME_TIMEOUT_SEC 200

/* Private variables -------------------------------------------------*/
static int s_recvSocket = -1;
static uint16_t s_recvPort = 0;
static std::atomic<bool> s_isRunning(false);
static T_DjiTaskHandle s_recvThreadHandle = NULL;
static UdpCommandHandler s_commandHandler = NULL;

/* Private functions declaration -------------------------------------*/
static void *ReceiverThreadEntry(void *arg);
static int DefaultCommandHandler(const T_ControlPacket *packet);

/* Exported functions definition -------------------------------------*/

T_DjiReturnCode UdpReceiver_Init(uint16_t port) {
  if (port == 0) {
    USER_LOG_ERROR("Invalid UDP receiver port");
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  // Create UDP socket
  s_recvSocket = socket(AF_INET, SOCK_DGRAM, 0);
  if (s_recvSocket < 0) {
    USER_LOG_ERROR("Failed to create UDP receiver socket");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  // Allow address reuse
  int opt = 1;
  setsockopt(s_recvSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // Bind to port
  struct sockaddr_in bindAddr;
  memset(&bindAddr, 0, sizeof(bindAddr));
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = INADDR_ANY;
  bindAddr.sin_port = htons(port);

  if (bind(s_recvSocket, (struct sockaddr *)&bindAddr, sizeof(bindAddr)) < 0) {
    USER_LOG_ERROR("Failed to bind UDP receiver to port %u", port);
    close(s_recvSocket);
    s_recvSocket = -1;
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  s_recvPort = port;

  // Set default handler if none registered
  if (s_commandHandler == NULL) {
    s_commandHandler = DefaultCommandHandler;
  }

  USER_LOG_INFO("UDP receiver initialized on port %u", port);
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode UdpReceiver_InitDefault(void) {
  return UdpReceiver_Init(UDP_RECEIVER_DEFAULT_PORT);
}

T_DjiReturnCode UdpReceiver_DeInit(void) {
  UdpReceiver_Stop();

  if (s_recvSocket >= 0) {
    close(s_recvSocket);
    s_recvSocket = -1;
  }

  USER_LOG_INFO("UDP receiver deinitialized");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode UdpReceiver_Start(void) {
  T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();
  T_DjiReturnCode returnCode;

  if (s_recvSocket < 0) {
    USER_LOG_ERROR("UDP receiver not initialized");
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT_IN_CURRENT_STATE;
  }

  if (s_isRunning) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // Already running
  }

  s_isRunning = true;

  // Create receiver thread
  returnCode = osalHandler->TaskCreate("udp_recv_thread", ReceiverThreadEntry,
                                       UDP_RECEIVER_THREAD_STACK_SIZE, NULL,
                                       &s_recvThreadHandle);

  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Failed to create UDP receiver thread");
    s_isRunning = false;
    return returnCode;
  }

  USER_LOG_INFO("UDP receiver started, listening on port %u", s_recvPort);
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode UdpReceiver_Stop(void) {
  T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();

  s_isRunning = false;

  // Close socket to unblock recvfrom
  if (s_recvSocket >= 0) {
    shutdown(s_recvSocket, SHUT_RDWR);
  }

  // Wait for thread to exit
  if (s_recvThreadHandle != NULL) {
    osalHandler->TaskSleepMs(100);
    osalHandler->TaskDestroy(s_recvThreadHandle);
    s_recvThreadHandle = NULL;
  }

  USER_LOG_INFO("UDP receiver stopped");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode UdpReceiver_RegisterCommandHandler(UdpCommandHandler handler) {
  s_commandHandler = handler;
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

bool UdpReceiver_IsRunning(void) { return s_isRunning; }

/* Private functions definition --------------------------------------*/

static void *ReceiverThreadEntry(void *arg) {
  (void)arg;
  uint8_t buffer[1024];
  struct sockaddr_in senderAddr;
  socklen_t senderAddrLen = sizeof(senderAddr);

  USER_LOG_INFO("UDP receiver thread started");

  // Heartbeat tracking variables
  auto lastHeartbeatTime = std::chrono::steady_clock::now();
  int missedHeartbeats = 0;
  bool hasStopped = false;
  bool hasGoHome = false;
  bool heartbeatReceivedEver = false;
  bool heartbeatLostLogged = false;

  // Set receive timeout
  struct timeval tv;
  tv.tv_sec = 1;
  tv.tv_usec = 0;
  setsockopt(s_recvSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  while (s_isRunning) {
    ssize_t bytesRead =
        recvfrom(s_recvSocket, buffer, sizeof(buffer) - 1, 0,
                 (struct sockaddr *)&senderAddr, &senderAddrLen);

    auto now = std::chrono::steady_clock::now();

    if (bytesRead > 0) {
      buffer[bytesRead] = '\0';

      // Check if it's a JSON heartbeat packet
      if (buffer[0] == '{' && strstr((char *)buffer, "\"heartbeat\"") != NULL) {
        if (!heartbeatReceivedEver) {
          USER_LOG_INFO("UDP heartbeat detected for the first time");
        } else if (heartbeatLostLogged) {
          USER_LOG_INFO("UDP heartbeat restored");
        }
        lastHeartbeatTime = now;
        missedHeartbeats = 0;
        hasStopped = false;
        hasGoHome = false;
        heartbeatReceivedEver = true;
        heartbeatLostLogged = false;
        // Successfully processed heartbeat, continue to next loop iteration
        continue;
      }

      if (bytesRead != PROTOCOL_PACKET_SIZE) {
        USER_LOG_WARN("UDP received incomplete packet or unknown JSON: %zd bytes", bytesRead);
      } else {
        // Validate packet
        T_ControlPacket *packet = (T_ControlPacket *)buffer;
        if (!Protocol_ValidatePacket(packet)) {
          USER_LOG_WARN("UDP received invalid packet (header: 0x%04X)",
                        packet->header);
        } else {
          char senderIp[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &senderAddr.sin_addr, senderIp, INET_ADDRSTRLEN);
          USER_LOG_INFO("UDP received command: %s (0x%02X) from %s",
                        Protocol_GetCommandName(packet->cmdType), packet->cmdType,
                        senderIp);

          // Execute command
          if (s_commandHandler != NULL) {
            int result = s_commandHandler(packet);
            if (result != 0) {
              USER_LOG_WARN("UDP command handler returned error: %d", result);
            }
          }
        }
      }
    }

    // Check heartbeat timeout
    if (heartbeatReceivedEver) {
      auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeatTime).count();
      missedHeartbeats = elapsedMs / 1000;

      if (elapsedMs >= UDP_HEARTBEAT_STOP_TIMEOUT_SEC * 1000 && !heartbeatLostLogged) {
        USER_LOG_WARN("UDP heartbeat not detected for %.1f seconds",
                      elapsedMs / 1000.0);
        heartbeatLostLogged = true;
      }

      if (missedHeartbeats >= UDP_HEARTBEAT_STOP_TIMEOUT_SEC && !hasStopped) {
        USER_LOG_ERROR("UDP heartbeat missing over %d seconds, triggering navigation stop",
                       UDP_HEARTBEAT_STOP_TIMEOUT_SEC);
        CommandControl_StopNavigation();
        hasStopped = true;
      }
      if (missedHeartbeats >= UDP_HEARTBEAT_GOHOME_TIMEOUT_SEC && !hasGoHome) {
        USER_LOG_ERROR("UDP heartbeat missing over %d seconds, triggering GoHome",
                       UDP_HEARTBEAT_GOHOME_TIMEOUT_SEC);
        CommandControl_GoHome();
        hasGoHome = true;
      }
    }
  }

  USER_LOG_INFO("UDP receiver thread exited");
  return NULL;
}

static int DefaultCommandHandler(const T_ControlPacket *packet) {
  if (packet == NULL) {
    return -1;
  }

  // Use ConsoleHandler to execute the command (unified execution path)
  T_DjiReturnCode ret = ConsoleHandler_ExecuteFromPacket(packet);

  return (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) ? 0 : -1;
}

/************************ (C) COPYRIGHT DJI Innovations *****END OF FILE****/
