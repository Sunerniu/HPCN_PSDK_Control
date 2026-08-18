/**
 ********************************************************************
 * @file    udp_sender.cpp
 * @brief   UDP数据发送模块实现
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 *********************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "udp_sender.hpp"
#include "time_sync_bridge.hpp"
#include "command_control.hpp"
#include "data.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <dji_logger.h>
#include <dji_platform.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

/* Private constants ---------------------------------------------------------*/
#define UDP_BUFFER_SIZE 4096
#define UDP_STATUS_THREAD_STACK_SIZE 2048
#define UDP_SEND_LOG_INTERVAL_MS 100000000

/* Private variables ---------------------------------------------------------*/
static int s_udpSocket = -1;
static struct sockaddr_in s_targetAddr;
static bool s_isInitialized = false;
static char s_targetIp[64] = {0};
static uint16_t s_targetPort = 0;
static T_DjiTaskHandle s_statusThreadHandle = NULL;
static std::atomic<bool> s_statusThreadRunning(false);
static std::atomic<uint32_t> s_importantIntervalMs(
    UDP_IMPORTANT_SEND_INTERVAL_MS);
static std::atomic<uint32_t> s_fullIntervalMs(UDP_FULL_SEND_INTERVAL_MS);
static std::chrono::steady_clock::time_point s_lastSendLogTime;

/* Private functions declaration ---------------------------------------------*/
static T_DjiReturnCode ValidateSendRates(uint32_t importantIntervalMs,
                                         uint32_t fullIntervalMs);
static T_DjiReturnCode SendJsonBuffer(const char *buffer, int jsonLen);
static T_DjiReturnCode SendImportantDroneStatus(const T_DroneStatus *status);
static int FormatImportantDroneStatusToJson(const T_DroneStatus *status,
                                            char *buffer, size_t bufferSize);
static int FormatFullDroneStatusToJson(const T_DroneStatus *status,
                                       const T_NavState *navState,
                                       char *buffer, size_t bufferSize);
static void AppendRealTimeJson(const char *fieldName,
                               const T_DjiDataTimestamp *timestamp,
                               char *buffer, size_t bufferSize) {
  T_TimeSyncBridgeRealTime realTime;
  char isoTime[40];

  if (fieldName == NULL || timestamp == NULL || buffer == NULL ||
      bufferSize == 0) {
    return;
  }

  if (TimeSyncBridge_ConvertDataTimestamp(timestamp, &realTime) ==
      DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS && realTime.valid) {
    TimeSyncBridge_FormatIso8601(&realTime, isoTime, sizeof(isoTime));
    snprintf(buffer, bufferSize,
             ",\"%s\":{\"valid\":true,\"iso8601\":\"%s\",\"epoch_us\":%llu}",
             fieldName, isoTime, (unsigned long long)realTime.epochUs);
  } else {
    snprintf(buffer, bufferSize, ",\"%s\":{\"valid\":false}",
             fieldName);
  }
}

static int FormatGpsRtkJsonBlock(const T_DroneStatus *status, char *buffer,
                                 size_t bufferSize);
static bool IsValidGlobalCoordinateLocal(dji_f64_t latitude,
                                         dji_f64_t longitude);
static bool IsGpsPositionValid(const T_DroneStatus *status);
static bool IsRtkPositionValid(const T_DroneStatus *status);
/* Exported functions definition ---------------------------------------------*/
T_DjiReturnCode UdpSender_Init(const char *targetIp, uint16_t targetPort) {
  if (targetIp == NULL || targetPort == 0) {
    USER_LOG_ERROR("Invalid UDP target parameters");
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  s_importantIntervalMs.store(UDP_IMPORTANT_SEND_INTERVAL_MS);
  s_fullIntervalMs.store(UDP_FULL_SEND_INTERVAL_MS);
  s_lastSendLogTime = std::chrono::steady_clock::now() -
                      std::chrono::milliseconds(UDP_SEND_LOG_INTERVAL_MS);

  // 创建UDP socket
  s_udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
  if (s_udpSocket < 0) {
    USER_LOG_ERROR("Failed to create UDP socket");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  // 设置目标地址
  memset(&s_targetAddr, 0, sizeof(s_targetAddr));
  s_targetAddr.sin_family = AF_INET;
  s_targetAddr.sin_port = htons(targetPort);

  if (inet_pton(AF_INET, targetIp, &s_targetAddr.sin_addr) <= 0) {
    USER_LOG_ERROR("Invalid target IP address: %s", targetIp);
    close(s_udpSocket);
    s_udpSocket = -1;
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  // 保存配置
  strncpy(s_targetIp, targetIp, sizeof(s_targetIp) - 1);
  s_targetPort = targetPort;
  s_isInitialized = true;

  USER_LOG_INFO("UDP sender initialized: %s:%u", targetIp, targetPort);
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode UdpSender_InitWithConfig(const T_UdpSenderConfig *config) {
  T_DjiReturnCode returnCode;

  if (config == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  returnCode =
      ValidateSendRates(config->importantIntervalMs, config->fullIntervalMs);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    return returnCode;
  }

  returnCode = UdpSender_Init(config->targetIp, config->targetPort);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    return returnCode;
  }

  return UdpSender_SetSendRates(config->importantIntervalMs,
                                config->fullIntervalMs);
}

T_DjiReturnCode UdpSender_SetSendRates(uint32_t importantIntervalMs,
                                       uint32_t fullIntervalMs) {
  T_DjiReturnCode returnCode =
      ValidateSendRates(importantIntervalMs, fullIntervalMs);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    return returnCode;
  }

  s_importantIntervalMs.store(importantIntervalMs);
  s_fullIntervalMs.store(fullIntervalMs);
  USER_LOG_INFO("UDP send rates updated: important=%ums, full=%ums",
                (unsigned int)importantIntervalMs,
                (unsigned int)fullIntervalMs);
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode UdpSender_DeInit(void) {
  if (s_udpSocket >= 0) {
    close(s_udpSocket);
    s_udpSocket = -1;
  }

  s_isInitialized = false;
  USER_LOG_INFO("UDP sender deinitialized");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode UdpSender_SendDroneStatus(const T_DroneStatus *status) {
  char buffer[UDP_BUFFER_SIZE];
  int jsonLen;

  if (!s_isInitialized) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT_IN_CURRENT_STATE;
  }

  if (status == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  // Format to JSON (including nav state)
  T_NavState navState;
  CommandControl_GetNavState(&navState);
  jsonLen =
      FormatFullDroneStatusToJson(status, &navState, buffer, sizeof(buffer));
  if (jsonLen <= 0) {
    USER_LOG_ERROR("Failed to format drone status to JSON");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  return SendJsonBuffer(buffer, jsonLen);
}

T_DjiReturnCode UdpSender_SendLatestStatus(void) {
  T_DroneStatus status;
  T_DjiReturnCode returnCode;

  returnCode = DataSubscriber_GetDroneStatus(&status);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    return returnCode;
  }

  return UdpSender_SendDroneStatus(&status);
}

bool UdpSender_IsInitialized(void) { return s_isInitialized; }

T_DjiReturnCode UdpSender_InitDefault(void) {
  const char *targetIp = std::getenv("UDP_TARGET_IP");
  if (targetIp == NULL || targetIp[0] == '\0') {
    targetIp = UDP_DEFAULT_IP;
  }

  return UdpSender_Init(targetIp, UDP_DEFAULT_PORT);
}

static void *StatusThreadEntry(void *arg) {
  T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();
  auto lastImportantSendTime = std::chrono::steady_clock::now();
  auto lastFullSendTime = lastImportantSendTime;
  (void)arg;

  USER_LOG_INFO("UDP status monitor thread started");

  while (s_statusThreadRunning) {
    auto now = std::chrono::steady_clock::now();
    uint32_t importantIntervalMs = s_importantIntervalMs.load();
    uint32_t fullIntervalMs = s_fullIntervalMs.load();
    int64_t importantElapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastImportantSendTime)
            .count();
    int64_t fullElapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastFullSendTime)
            .count();
    bool fullDue = fullElapsedMs >= (int64_t)fullIntervalMs;
    bool importantDue = importantElapsedMs >= (int64_t)importantIntervalMs;

    if (s_isInitialized) {
      if (fullDue) {
        if (UdpSender_SendLatestStatus() == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
          lastFullSendTime = now;
          lastImportantSendTime = now;
        }
      } else if (importantDue) {
        T_DroneStatus status;
        if (DataSubscriber_GetDroneStatus(&status) ==
            DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
          if (SendImportantDroneStatus(&status) ==
              DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            lastImportantSendTime = now;
          }
        }
      }
    }

    uint32_t sleepMs = importantIntervalMs < 10 ? importantIntervalMs : 10;
    osalHandler->TaskSleepMs(sleepMs > 0 ? sleepMs : 1);
  }

  USER_LOG_INFO("UDP status monitor thread exited");
  return NULL;
}

T_DjiReturnCode UdpSender_StartStatusThread(void) {
  T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();
  T_DjiReturnCode returnCode;

  if (s_statusThreadHandle != NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS; // 已经运行
  }

  s_statusThreadRunning = true;

  returnCode = osalHandler->TaskCreate("udp_status_thread", StatusThreadEntry,
                                       UDP_STATUS_THREAD_STACK_SIZE, NULL,
                                       &s_statusThreadHandle);

  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Create UDP status thread failed");
    s_statusThreadRunning = false;
    return returnCode;
  }

  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode UdpSender_StopStatusThread(void) {
  T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();

  s_statusThreadRunning = false;

  if (s_statusThreadHandle != NULL) {
    osalHandler->TaskSleepMs(20);
    osalHandler->TaskDestroy(s_statusThreadHandle);
    s_statusThreadHandle = NULL;
  }

  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

/* Private functions definition-----------------------------------------------*/
static T_DjiReturnCode ValidateSendRates(uint32_t importantIntervalMs,
                                         uint32_t fullIntervalMs) {
  if (importantIntervalMs == 0 || fullIntervalMs == 0 ||
      fullIntervalMs < importantIntervalMs) {
    USER_LOG_ERROR("Invalid UDP send rates: important=%ums, full=%ums",
                   (unsigned int)importantIntervalMs,
                   (unsigned int)fullIntervalMs);
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static T_DjiReturnCode SendJsonBuffer(const char *buffer, int jsonLen) {
  ssize_t sentBytes;

  if (!s_isInitialized) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT_IN_CURRENT_STATE;
  }

  if (buffer == NULL || jsonLen <= 0) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  sentBytes = sendto(s_udpSocket, buffer, jsonLen, 0,
                     (struct sockaddr *)&s_targetAddr, sizeof(s_targetAddr));
  if (sentBytes < 0) {
    USER_LOG_ERROR("Failed to send UDP packet");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  auto now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(
          now - s_lastSendLogTime)
          .count() >= UDP_SEND_LOG_INTERVAL_MS) {
    s_lastSendLogTime = now;
    USER_LOG_INFO("UDP sent %zd bytes to %s:%u, payload: %s", sentBytes,
                  s_targetIp, s_targetPort, buffer);
  }

  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static T_DjiReturnCode SendImportantDroneStatus(const T_DroneStatus *status) {
  char buffer[UDP_BUFFER_SIZE];
  int jsonLen;

  if (status == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  jsonLen = FormatImportantDroneStatusToJson(status, buffer, sizeof(buffer));
  if (jsonLen <= 0) {
    USER_LOG_ERROR("Failed to format important drone status to JSON");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  return SendJsonBuffer(buffer, jsonLen);
}

static bool IsValidGlobalCoordinateLocal(dji_f64_t latitude,
                                         dji_f64_t longitude) {
  return latitude >= -90.0 && latitude <= 90.0 && longitude >= -180.0 &&
         longitude <= 180.0 && !(latitude == 0.0 && longitude == 0.0);
}

static bool IsGpsPositionValid(const T_DroneStatus *status) {
  dji_f64_t latitude;
  dji_f64_t longitude;
  bool hasGpsFix;

  if (status == NULL) {
    return false;
  }

  latitude = (dji_f64_t)status->gpsPosition.y / 10000000.0;
  longitude = (dji_f64_t)status->gpsPosition.x / 10000000.0;
  hasGpsFix = status->gpsDetails.fixState ==
                  DJI_FC_SUBSCRIPTION_GPS_FIX_STATE_3D_FIX ||
              status->gpsDetails.fixState ==
                  DJI_FC_SUBSCRIPTION_GPS_FIX_STATE_GPS_PLUS_DEAD_RECKONING;

  return hasGpsFix && IsValidGlobalCoordinateLocal(latitude, longitude);
}

static bool IsRtkPositionValid(const T_DroneStatus *status) {
  if (status == NULL) {
    return false;
  }

  return status->rtkPositionInfo != 0 &&
         IsValidGlobalCoordinateLocal(status->rtkPosition.latitude,
                                      status->rtkPosition.longitude);
}

static int FormatGpsRtkJsonBlock(const T_DroneStatus *status, char *buffer,
                                 size_t bufferSize) {
  bool gpsValid;
  bool rtkValid;
  dji_f64_t gpsLatitude;
  dji_f64_t gpsLongitude;
  dji_f64_t gpsAltitude;
  int len;

  if (status == NULL || buffer == NULL || bufferSize == 0) {
    return -1;
  }

  gpsValid = IsGpsPositionValid(status);
  rtkValid = IsRtkPositionValid(status);
  gpsLatitude = (dji_f64_t)status->gpsPosition.y / 10000000.0;
  gpsLongitude = (dji_f64_t)status->gpsPosition.x / 10000000.0;
  gpsAltitude = (dji_f64_t)status->gpsPosition.z / 1000.0;

  len = snprintf(buffer, bufferSize,
                 "\"gps\":{"
                 "\"valid\":%s,"
                 "\"lat\":%.7f,"
                 "\"lon\":%.7f,"
                 "\"alt\":%.2f,"
                 "\"fix_state\":%.0f,"
                 "\"position_time\":{\"ms\":%u,\"us\":%u},"
                 "\"details_time\":{\"ms\":%u,\"us\":%u}",
                 gpsValid ? "true" : "false", gpsLatitude, gpsLongitude,
                 gpsAltitude, status->gpsDetails.fixState,
                 (unsigned int)status->gpsPositionTimestamp.millisecond,
                 (unsigned int)status->gpsPositionTimestamp.microsecond,
                 (unsigned int)status->gpsDetailsTimestamp.millisecond,
                 (unsigned int)status->gpsDetailsTimestamp.microsecond);
  if (len <= 0 || (size_t)len >= bufferSize) {
    return -1;
  }

  AppendRealTimeJson("position_real_time", &status->gpsPositionTimestamp,
                     buffer + len, bufferSize - len);
  len += strlen(buffer + len);
  if ((size_t)len >= bufferSize) {
    return -1;
  }

  len += snprintf(buffer + len, bufferSize - len,
                  "},\"rtk\":{"
                  "\"valid\":%s,"
                  "\"lat\":%.7f,"
                  "\"lon\":%.7f,"
                  "\"alt\":%.2f,"
                  "\"position_info\":%u,"
                  "\"position_time\":{\"ms\":%u,\"us\":%u},"
                  "\"info_time\":{\"ms\":%u,\"us\":%u}",
                  rtkValid ? "true" : "false", status->rtkPosition.latitude,
                  status->rtkPosition.longitude, status->rtkPosition.hfsl,
                  (unsigned int)status->rtkPositionInfo,
                  (unsigned int)status->rtkPositionTimestamp.millisecond,
                  (unsigned int)status->rtkPositionTimestamp.microsecond,
                  (unsigned int)status->rtkInfoTimestamp.millisecond,
                  (unsigned int)status->rtkInfoTimestamp.microsecond);
  if (len <= 0 || (size_t)len >= bufferSize) {
    return -1;
  }

  AppendRealTimeJson("position_real_time", &status->rtkPositionTimestamp,
                     buffer + len, bufferSize - len);
  len += strlen(buffer + len);
  if ((size_t)len >= bufferSize - 1) {
    return -1;
  }

  buffer[len++] = '}';
  buffer[len] = '\0';
  return len;
}

static int FormatImportantDroneStatusToJson(const T_DroneStatus *status,
                                            char *buffer, size_t bufferSize) {
  time_t now;
  int len;
  int blockLen;

  if (status == NULL || buffer == NULL || bufferSize == 0) {
    return -1;
  }

  now = time(NULL);
  len = snprintf(buffer, bufferSize,
                 "{"
                 "\"packet_time\":%ld,"
                 "\"position_source\":\"%s\",",
                 (long)now,
                 DataSubscriber_GetPositionSourceString(
                     status->positionSource));
  if (len <= 0 || (size_t)len >= bufferSize) {
    return -1;
  }

  blockLen = FormatGpsRtkJsonBlock(status, buffer + len, bufferSize - len);
  if (blockLen <= 0 || (size_t)(len + blockLen) >= bufferSize) {
    return -1;
  }
  len += blockLen;

  if ((size_t)len >= bufferSize - 1) {
    return -1;
  }
  buffer[len++] = '}';
  buffer[len] = '\0';
  return len;
}

static int FormatFullDroneStatusToJson(const T_DroneStatus *status,
                                       const T_NavState *navState,
                                       char *buffer, size_t bufferSize) {
  if (status == NULL || buffer == NULL || bufferSize == 0) {
    return -1;
  }

  // 获取时间戳
  time_t now = time(NULL);

  // 格式化JSON字符串 (基础部分)
  int len = snprintf(
      buffer, bufferSize,
      "{"
      "\"packet_time\":%ld,"
      "\"attitude\":{"
      "\"pitch\":%.4f,"
      "\"roll\":%.4f,"
      "\"yaw\":%.4f"
      "},"
      "\"velocity\":{"
      "\"vx\":%.3f,"
      "\"vy\":%.3f,"
      "\"vz\":%.3f"
      "},"
      "\"battery\":{"
      "\"voltage\":%d,"
      "\"current\":%d,"
      "\"percent\":%d,"
      "\"capacity\":%u"
      "},"
      "\"flight_status\":%d,"
      "\"display_mode\":%d,"
      "\"height\":%.2f,"
      "\"home_set\":%d,"
      "\"quaternion\":{"
      "\"q0\":%.6f,"
      "\"q1\":%.6f,"
      "\"q2\":%.6f,"
      "\"q3\":%.6f"
      "},"
      "\"gimbal\":{"
      "\"pitch\":%.2f,"
      "\"roll\":%.2f,"
      "\"yaw\":%.2f"
      "},"
      "\"position_source\":\"%s\",",
      (long)now, status->pitch, status->roll, status->yaw,
      status->velocity.data.x, status->velocity.data.y, status->velocity.data.z,
      status->batteryInfo.voltage, status->batteryInfo.current,
      status->batteryInfo.percentage, status->batteryInfo.capacity,
      status->flightStatus, status->displayMode, status->heightFusion,
      status->homePointSetStatus, status->quaternion.q0, status->quaternion.q1,
      status->quaternion.q2, status->quaternion.q3, status->gimbalPitch,
      status->gimbalRoll, status->gimbalYaw,
      DataSubscriber_GetPositionSourceString(status->positionSource));

  if (len <= 0 || (size_t)len >= bufferSize) {
    return -1;
  }

  int gpsRtkLen = FormatGpsRtkJsonBlock(status, buffer + len, bufferSize - len);
  if (gpsRtkLen <= 0 || (size_t)(len + gpsRtkLen) >= bufferSize) {
    return -1;
  }
  len += gpsRtkLen;

  // Add navigation state
  if (navState != NULL && len > 0 && (size_t)len < bufferSize - 1) {
    int navLen = snprintf(buffer + len, bufferSize - len,
                          ","
                          "\"navigation\":{"
                          "\"status\":\"%s\","
                          "\"is_navigating\":%s,"
                          "\"has_target\":%s,"
                          "\"last_error\":%lld,"
                          "\"current_target\":{"
                          "\"lat\":%.7f,"
                          "\"lon\":%.7f,"
                          "\"alt\":%.2f,"
                          "\"speed\":%.1f"
                          "},"
                          "\"queue_count\":%d,"
                          "\"queue\":[",
                          CommandControl_GetNavigationStatusString(
                              navState->status),
                          navState->isNavigating ? "true" : "false",
                          navState->hasActiveTarget ? "true" : "false",
                          (long long)navState->lastErrorCode,
                          navState->currentTarget.latitude,
                          navState->currentTarget.longitude,
                          navState->currentTarget.altitude,
                          navState->currentTarget.speed, navState->queueCount);
    if (navLen <= 0 || (size_t)(len + navLen) >= bufferSize) {
      return -1;
    }
    len += navLen;

    // Add queue items
    for (int i = 0; i < navState->queueCount && (size_t)len < bufferSize - 50;
         i++) {
      int itemLen =
          snprintf(buffer + len, bufferSize - len,
                   "%s{\"lat\":%.7f,\"lon\":%.7f,\"alt\":%.2f}",
                   (i > 0) ? "," : "", navState->queue[i].latitude,
                   navState->queue[i].longitude, navState->queue[i].altitude);
      if (itemLen <= 0 || (size_t)(len + itemLen) >= bufferSize) {
        return -1;
      }
      len += itemLen;
    }

    // Close queue array and navigation object
    int closeLen = snprintf(buffer + len, bufferSize - len, "]}");
    if (closeLen <= 0 || (size_t)(len + closeLen) >= bufferSize) {
      return -1;
    }
    len += closeLen;
  }

  // Close main JSON object
  if ((size_t)len >= bufferSize - 1) {
    return -1;
  }

  buffer[len++] = '}';
  buffer[len] = '\0';
  return len;
}

/************************ (C) COPYRIGHT DJI Innovations *****END OF FILE****/
