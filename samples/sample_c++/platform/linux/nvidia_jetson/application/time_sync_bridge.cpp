/**
 ********************************************************************
 * @file    time_sync_bridge.cpp
 * @brief   PPS/GPIO time synchronization bridge for GPS/RTK timestamps.
 *********************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "time_sync_bridge.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dji_logger.h>
#include <dji_time_sync.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Private constants ---------------------------------------------------------*/
#define PPS_GPIOCHIP_ENV "PPS_GPIOCHIP"
#define PPS_GPIO_LINE_ENV "PPS_GPIO_LINE"
#define PPS_CONSUMER_LABEL "hpcn-psdk-pps"

/* Private variables ---------------------------------------------------------*/
static std::atomic<uint64_t> s_lastPpsLocalTimeUs(0);
static std::atomic<bool> s_ppsValid(false);
static std::atomic<bool> s_threadRunning(false);
static std::atomic<bool> s_timeSyncReady(false);
static std::atomic<int> s_eventFd(-1);
static pthread_t s_ppsThread;
static bool s_threadCreated = false;

/* Private functions declaration ---------------------------------------------*/
static void *PpsCaptureThread(void *arg);
static T_DjiReturnCode GetNewestPpsTriggerLocalTimeUs(uint64_t *localTimeUs);
static uint64_t GetMonotonicTimeUs(void);
static bool BuildEpochUs(const T_DjiTimeSyncAircraftTime *aircraftTime,
                         uint64_t *epochUs);

/* Exported functions definition ---------------------------------------------*/
T_DjiReturnCode TimeSyncBridge_Init(void) {
  const char *gpioChip = getenv(PPS_GPIOCHIP_ENV);
  const char *gpioLineText = getenv(PPS_GPIO_LINE_ENV);
  char *endPtr = NULL;
  long gpioLine;
  int chipFd;
  struct gpioevent_request request;
  int result;
  T_DjiReturnCode returnCode;

  if (s_timeSyncReady.load()) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
  }

  if (gpioChip == NULL || gpioLineText == NULL) {
    USER_LOG_WARN("PPS time sync disabled. Set %s=/dev/gpiochipX and %s=line",
                  PPS_GPIOCHIP_ENV, PPS_GPIO_LINE_ENV);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  errno = 0;
  gpioLine = strtol(gpioLineText, &endPtr, 10);
  if (errno != 0 || endPtr == gpioLineText || *endPtr != '\0' ||
      gpioLine < 0) {
    USER_LOG_ERROR("Invalid %s value: %s", PPS_GPIO_LINE_ENV, gpioLineText);
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  chipFd = open(gpioChip, O_RDONLY | O_CLOEXEC);
  if (chipFd < 0) {
    USER_LOG_ERROR("Open PPS gpiochip failed: %s, errno=%d", gpioChip, errno);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  memset(&request, 0, sizeof(request));
  request.lineoffset = (uint32_t)gpioLine;
  request.handleflags = GPIOHANDLE_REQUEST_INPUT;
  request.eventflags = GPIOEVENT_REQUEST_RISING_EDGE;
  strncpy(request.consumer_label, PPS_CONSUMER_LABEL,
          sizeof(request.consumer_label) - 1);

  result = ioctl(chipFd, GPIO_GET_LINEEVENT_IOCTL, &request);
  close(chipFd);
  if (result < 0) {
    USER_LOG_ERROR("Request PPS GPIO rising edge failed: %s line %ld, errno=%d",
                   gpioChip, gpioLine, errno);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  s_eventFd.store(request.fd);
  s_threadRunning.store(true);
  result = pthread_create(&s_ppsThread, NULL, PpsCaptureThread, NULL);
  if (result != 0) {
    close(request.fd);
    s_eventFd.store(-1);
    s_threadRunning.store(false);
    USER_LOG_ERROR("Create PPS capture thread failed: %d", result);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }
  s_threadCreated = true;

  returnCode = DjiTimeSync_Init();
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("DjiTimeSync_Init failed: 0x%08llX", returnCode);
    TimeSyncBridge_DeInit();
    return returnCode;
  }

  returnCode = DjiTimeSync_RegGetNewestPpsTriggerTimeCallback(
      GetNewestPpsTriggerLocalTimeUs);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Register PPS callback failed: 0x%08llX", returnCode);
    TimeSyncBridge_DeInit();
    return returnCode;
  }

  s_timeSyncReady.store(true);
  USER_LOG_INFO("PPS time sync bridge initialized: %s line %ld", gpioChip,
                gpioLine);
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode TimeSyncBridge_DeInit(void) {
  int eventFd = s_eventFd.exchange(-1);

  s_timeSyncReady.store(false);
  s_threadRunning.store(false);

  if (eventFd >= 0) {
    close(eventFd);
  }

  if (s_threadCreated) {
    pthread_join(s_ppsThread, NULL);
    s_threadCreated = false;
  }

  s_ppsValid.store(false);
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

bool TimeSyncBridge_IsReady(void) {
  return s_timeSyncReady.load() && s_ppsValid.load();
}

T_DjiReturnCode
TimeSyncBridge_ConvertDataTimestamp(const T_DjiDataTimestamp *timestamp,
                                    T_TimeSyncBridgeRealTime *realTime) {
  uint64_t localTimeUs;
  T_DjiTimeSyncAircraftTime aircraftTime;
  T_DjiReturnCode returnCode;

  if (timestamp == NULL || realTime == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  memset(realTime, 0, sizeof(T_TimeSyncBridgeRealTime));
  if (!TimeSyncBridge_IsReady()) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  localTimeUs = (uint64_t)timestamp->millisecond * 1000ULL +
                (uint64_t)timestamp->microsecond;
  returnCode = DjiTimeSync_TransferToAircraftTime(localTimeUs, &aircraftTime);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    return returnCode;
  }

  realTime->valid = true;
  realTime->year = aircraftTime.year;
  realTime->month = aircraftTime.month;
  realTime->day = aircraftTime.day;
  realTime->hour = aircraftTime.hour;
  realTime->minute = aircraftTime.minute;
  realTime->second = aircraftTime.second;
  realTime->microsecond = aircraftTime.microsecond;
  BuildEpochUs(&aircraftTime, &realTime->epochUs);

  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

void TimeSyncBridge_FormatIso8601(const T_TimeSyncBridgeRealTime *realTime,
                                  char *buffer, size_t bufferSize) {
  if (buffer == NULL || bufferSize == 0) {
    return;
  }

  if (realTime == NULL || !realTime->valid) {
    buffer[0] = '\0';
    return;
  }

  snprintf(buffer, bufferSize, "%04u-%02u-%02uT%02u:%02u:%02u.%06uZ",
           realTime->year, realTime->month, realTime->day, realTime->hour,
           realTime->minute, realTime->second, realTime->microsecond);
}

/* Private functions definition ----------------------------------------------*/
static void *PpsCaptureThread(void *arg) {
  (void)arg;

  while (s_threadRunning.load()) {
    struct gpioevent_data eventData;
    struct pollfd pollFd;
    int fd = s_eventFd.load();
    int pollResult;
    ssize_t readSize;
    uint64_t localTimeUs;

    if (fd < 0) {
      break;
    }

    pollFd.fd = fd;
    pollFd.events = POLLIN;
    pollFd.revents = 0;
    pollResult = poll(&pollFd, 1, 500);
    if (pollResult == 0 || (pollResult < 0 && errno == EINTR)) {
      continue;
    }
    if (pollResult < 0) {
      if (s_threadRunning.load()) {
        USER_LOG_ERROR("Poll PPS GPIO event failed, errno=%d", errno);
      }
      break;
    }

    readSize = read(fd, &eventData, sizeof(eventData));
    if (readSize == (ssize_t)sizeof(eventData)) {
      localTimeUs = eventData.timestamp / 1000ULL;
      if (localTimeUs == 0) {
        localTimeUs = GetMonotonicTimeUs();
      }

      s_lastPpsLocalTimeUs.store(localTimeUs, std::memory_order_release);
      s_ppsValid.store(true, std::memory_order_release);
    } else if (readSize < 0 && errno != EINTR) {
      if (s_threadRunning.load()) {
        USER_LOG_ERROR("Read PPS GPIO event failed, errno=%d", errno);
      }
      break;
    }
  }

  return NULL;
}

static T_DjiReturnCode GetNewestPpsTriggerLocalTimeUs(uint64_t *localTimeUs) {
  if (localTimeUs == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  if (!s_ppsValid.load(std::memory_order_acquire)) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  *localTimeUs = s_lastPpsLocalTimeUs.load(std::memory_order_acquire);
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static uint64_t GetMonotonicTimeUs(void) {
  struct timespec time;

  clock_gettime(CLOCK_MONOTONIC, &time);
  return (uint64_t)time.tv_sec * 1000000ULL +
         (uint64_t)time.tv_nsec / 1000ULL;
}

static bool BuildEpochUs(const T_DjiTimeSyncAircraftTime *aircraftTime,
                         uint64_t *epochUs) {
  struct tm utcTime;
  time_t epochSec;

  if (aircraftTime == NULL || epochUs == NULL) {
    return false;
  }

  memset(&utcTime, 0, sizeof(utcTime));
  utcTime.tm_year = aircraftTime->year - 1900;
  utcTime.tm_mon = aircraftTime->month - 1;
  utcTime.tm_mday = aircraftTime->day;
  utcTime.tm_hour = aircraftTime->hour;
  utcTime.tm_min = aircraftTime->minute;
  utcTime.tm_sec = aircraftTime->second;

  epochSec = timegm(&utcTime);
  if (epochSec < 0) {
    return false;
  }

  *epochUs = (uint64_t)epochSec * 1000000ULL + aircraftTime->microsecond;
  return true;
}
