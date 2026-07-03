/**
 ********************************************************************
 * @file    tcp_server.cpp
 * @brief   TCP服务器模块实现 - AGX端接收PC控制命令
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 *********************************************************************
 */

/* Includes ----------------------------------------------------------*/
#include "tcp_server.hpp"
#include "command_control.hpp"
#include "console_handler.hpp"

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
#define TCP_SERVER_THREAD_STACK_SIZE 4096

/* Private variables -------------------------------------------------*/
static int s_serverSocket = -1;
static int s_clientSocket = -1;
static uint16_t s_serverPort = 0;
static std::atomic<bool> s_isRunning(false);
static std::atomic<E_TcpServerState> s_serverState(TCP_SERVER_STATE_IDLE);
static T_DjiTaskHandle s_serverThreadHandle = NULL;
static TcpCommandHandler s_commandHandler = NULL;

// 断线超时检测相关
static std::chrono::steady_clock::time_point s_lastDisconnectTime =
    std::chrono::steady_clock::now();
static std::atomic<bool> s_disconnectTimeoutTriggered(false);
static std::atomic<bool> s_hasClientConnected(false);

/* Private functions declaration -------------------------------------*/
static void *ServerThreadEntry(void *arg);
static int DefaultCommandHandler(const T_ControlPacket *packet);
static void HandleClientConnection(int clientSocket);

/* Exported functions definition -------------------------------------*/

T_DjiReturnCode TcpServer_Init(uint16_t port) {
  if (port == 0) {
    USER_LOG_ERROR("Invalid TCP server port");
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  // Create server socket
  s_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (s_serverSocket < 0) {
    USER_LOG_ERROR("Failed to create TCP server socket");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  // Allow address reuse
  int opt = 1;
  setsockopt(s_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // Bind to port
  struct sockaddr_in serverAddr;
  memset(&serverAddr, 0, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = INADDR_ANY;
  serverAddr.sin_port = htons(port);

  if (bind(s_serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) <
      0) {
    USER_LOG_ERROR("Failed to bind TCP server to port %u", port);
    close(s_serverSocket);
    s_serverSocket = -1;
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  s_serverPort = port;
  s_serverState = TCP_SERVER_STATE_IDLE;

  // Set default handler if none registered
  if (s_commandHandler == NULL) {
    s_commandHandler = DefaultCommandHandler;
  }

  USER_LOG_INFO("TCP server initialized on port %u", port);
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode TcpServer_DeInit(void) {
  TcpServer_Stop();

  if (s_serverSocket >= 0) {
    close(s_serverSocket);
    s_serverSocket = -1;
  }

  s_serverState = TCP_SERVER_STATE_IDLE;
  USER_LOG_INFO("TCP server deinitialized");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode TcpServer_Start(void) {
  T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();
  T_DjiReturnCode returnCode;

  if (s_serverSocket < 0) {
    USER_LOG_ERROR("TCP server not initialized");
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT_IN_CURRENT_STATE;
  }

  if (s_isRunning) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // Already running
  }

  // Start listening
  if (listen(s_serverSocket, TCP_SERVER_MAX_CLIENTS) < 0) {
    USER_LOG_ERROR("Failed to listen on TCP server socket");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  s_isRunning = true;
  s_serverState = TCP_SERVER_STATE_LISTENING;
  s_lastDisconnectTime = std::chrono::steady_clock::now();
  s_disconnectTimeoutTriggered = false;
  s_hasClientConnected = false;

  // Create server thread
  returnCode = osalHandler->TaskCreate("tcp_server_thread", ServerThreadEntry,
                                       TCP_SERVER_THREAD_STACK_SIZE, NULL,
                                       &s_serverThreadHandle);

  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Failed to create TCP server thread");
    s_isRunning = false;
    s_serverState = TCP_SERVER_STATE_ERROR;
    return returnCode;
  }

  USER_LOG_INFO("TCP server started, listening on port %u", s_serverPort);
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode TcpServer_InitAndStart(void) {
  T_DjiReturnCode returnCode;

  returnCode = TcpServer_Init(TCP_SERVER_DEFAULT_PORT);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    return returnCode;
  }

  return TcpServer_Start();
}

T_DjiReturnCode TcpServer_Stop(void) {
  T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();

  s_isRunning = false;

  // Close client socket to unblock recv
  if (s_clientSocket >= 0) {
    close(s_clientSocket);
    s_clientSocket = -1;
  }

  // Wait for thread to exit
  if (s_serverThreadHandle != NULL) {
    osalHandler->TaskSleepMs(100);
    osalHandler->TaskDestroy(s_serverThreadHandle);
    s_serverThreadHandle = NULL;
  }

  s_serverState = TCP_SERVER_STATE_IDLE;
  USER_LOG_INFO("TCP server stopped");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

E_TcpServerState TcpServer_GetState(void) { return s_serverState; }

bool TcpServer_IsClientConnected(void) {
  return s_serverState == TCP_SERVER_STATE_CONNECTED;
}

T_DjiReturnCode TcpServer_RegisterCommandHandler(TcpCommandHandler handler) {
  s_commandHandler = handler;
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

/* Private functions definition --------------------------------------*/

static void *ServerThreadEntry(void *arg) {
  (void)arg;
  struct sockaddr_in clientAddr;
  socklen_t clientAddrLen = sizeof(clientAddr);
  fd_set readfds;
  struct timeval tv;

  USER_LOG_INFO("TCP server thread started");

  while (s_isRunning) {
    s_serverState = TCP_SERVER_STATE_LISTENING;

    // 使用select实现accept超时，以便定期检查断线超时
    FD_ZERO(&readfds);
    FD_SET(s_serverSocket, &readfds);
    tv.tv_sec = 1; // 1秒超时
    tv.tv_usec = 0;

    int selectResult = select(s_serverSocket + 1, &readfds, NULL, NULL, &tv);

    // 检查断线超时 (无客户端连接时)
    if (s_hasClientConnected && s_serverState != TCP_SERVER_STATE_CONNECTED &&
        !s_disconnectTimeoutTriggered) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - s_lastDisconnectTime)
                         .count();
      if (elapsed > TCP_SERVER_DISCONNECT_TIMEOUT_MS) {
        T_NavState navState;
        if (CommandControl_GetNavState(&navState) ==
                DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS &&
            navState.isNavigating) {
          USER_LOG_WARN(
              "TCP disconnect timeout (>%dms), sending navigation stop!",
              TCP_SERVER_DISCONNECT_TIMEOUT_MS);
          CommandControl_StopNavigationOnly();
        }
        s_disconnectTimeoutTriggered = true;
      }
    }

    if (selectResult <= 0) {
      // 超时或错误，继续循环
      continue;
    }

    // Accept client connection
    s_clientSocket =
        accept(s_serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);

    if (s_clientSocket < 0) {
      if (s_isRunning) {
        USER_LOG_WARN("TCP accept failed");
      }
      continue;
    }

    char clientIp[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
    USER_LOG_INFO("TCP client connected: %s:%u", clientIp,
                  ntohs(clientAddr.sin_port));

    s_serverState = TCP_SERVER_STATE_CONNECTED;
    s_hasClientConnected = true;
    s_disconnectTimeoutTriggered = false; // 重置断线超时标志

    // Handle client
    HandleClientConnection(s_clientSocket);

    // Client disconnected - 记录断开时间
    close(s_clientSocket);
    s_clientSocket = -1;
    s_lastDisconnectTime = std::chrono::steady_clock::now();
    s_disconnectTimeoutTriggered = false;
    USER_LOG_INFO("TCP client disconnected, starting disconnect timer");
  }

  USER_LOG_INFO("TCP server thread exited");
  return NULL;
}

static void HandleClientConnection(int clientSocket) {
  uint8_t buffer[PROTOCOL_PACKET_SIZE];
  ssize_t bytesRead;

  // Set receive timeout
  struct timeval tv;
  tv.tv_sec = TCP_SERVER_RECV_TIMEOUT_MS / 1000;
  tv.tv_usec = (TCP_SERVER_RECV_TIMEOUT_MS % 1000) * 1000;
  setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  while (s_isRunning && s_clientSocket >= 0) {
    bytesRead = recv(clientSocket, buffer, PROTOCOL_PACKET_SIZE, 0);

    if (bytesRead <= 0) {
      if (bytesRead == 0) {
        // Connection closed by client
        break;
      }
      // Timeout or error, continue waiting
      continue;
    }

    if (bytesRead != PROTOCOL_PACKET_SIZE) {
      USER_LOG_WARN("TCP received incomplete packet: %zd bytes", bytesRead);
      continue;
    }

    // Validate packet
    T_ControlPacket *packet = (T_ControlPacket *)buffer;
    if (!Protocol_ValidatePacket(packet)) {
      USER_LOG_WARN("TCP received invalid packet (header: 0x%04X)",
                    packet->header);
      continue;
    }

    USER_LOG_INFO("TCP received command: %s (0x%02X)",
                  Protocol_GetCommandName(packet->cmdType), packet->cmdType);

    // Execute command
    if (s_commandHandler != NULL) {
      int result = s_commandHandler(packet);
      if (result != 0) {
        USER_LOG_WARN("Command handler returned error: %d", result);
      }
    }
  }
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
