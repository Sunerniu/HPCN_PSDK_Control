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
#include <cstdint>
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
#include <sched.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Private constants ---------------------------------------------------------*/
#define PPS_GPIOCHIP_ENV "PPS_GPIOCHIP"
#define PPS_GPIO_LINE_ENV "PPS_GPIO_LINE"
#define AIRCRAFT_TIME_UTC_OFFSET_MINUTES_ENV                                    \
  "AIRCRAFT_TIME_UTC_OFFSET_MINUTES"
#define FC_TIME_TRANSPORT_DELAY_US_ENV "FC_TIME_TRANSPORT_DELAY_US"
#define PPS_CONSUMER_LABEL "hpcn-psdk-pps"
#define PPS_DEGLITCH_INTERVAL_US 500000ULL
#define PPS_FRESHNESS_TIMEOUT_US 2500000ULL
#define PPS_CAPTURE_THREAD_PRIORITY 60
#define FC_CLOCK_SAMPLE_COUNT 128
#define FC_CLOCK_MIN_VALID_SAMPLES 5

/* Private variables ---------------------------------------------------------*/
static std::atomic<uint64_t> s_lastPpsLocalTimeUs(0);
static std::atomic<bool> s_ppsValid(false);
static std::atomic<bool> s_threadRunning(false);
static std::atomic<bool> s_timeSyncReady(false);
static std::atomic<int> s_eventFd(-1);
static pthread_t s_ppsThread;
static bool s_threadCreated = false;
static int s_aircraftTimeUtcOffsetMinutes = 0;
static uint64_t s_fcTransportDelayUs = 0;

typedef struct {
  uint64_t flightControllerTimeUs;
  uint64_t localReceiveTimeUs;
} T_FcClockSample;

static T_FcClockSample s_fcClockSamples[FC_CLOCK_SAMPLE_COUNT];
static size_t s_fcClockSampleWriteIndex = 0;
static size_t s_fcClockSampleSize = 0;
static uint64_t s_lastFlightControllerTimeUs = 0;
static pthread_mutex_t s_ppsEventReadMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_fcClockSampleMutex = PTHREAD_MUTEX_INITIALIZER;

/* Private functions declaration ---------------------------------------------*/
static void *PpsCaptureThread(void *arg);
static void DrainPendingPpsEvents(void);
static void RecordPpsEventTimestamp(uint64_t eventTimestampNs);
static T_DjiReturnCode GetNewestPpsTriggerLocalTimeUs(uint64_t *localTimeUs);
static uint64_t GetMonotonicTimeUs(void);
static bool BuildEpochUs(const T_DjiTimeSyncAircraftTime *aircraftTime,
                         int utcOffsetMinutes, uint64_t *epochUs);

/* Exported functions definition ---------------------------------------------*/
T_DjiReturnCode TimeSyncBridge_Init(void) {
  const char *gpioChip = getenv(PPS_GPIOCHIP_ENV);
  const char *gpioLineText = getenv(PPS_GPIO_LINE_ENV);
  const char *utcOffsetText = getenv(AIRCRAFT_TIME_UTC_OFFSET_MINUTES_ENV);
  const char *fcTransportDelayText = getenv(FC_TIME_TRANSPORT_DELAY_US_ENV);
  char *endPtr = NULL;
  long gpioLine;
  long utcOffsetMinutes = 0;
  unsigned long long fcTransportDelayUs = 0;
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

  if (utcOffsetText != NULL) {
    errno = 0;
    endPtr = NULL;
    utcOffsetMinutes = strtol(utcOffsetText, &endPtr, 10);
    if (errno != 0 || endPtr == utcOffsetText || *endPtr != '\0' ||
        utcOffsetMinutes < -14 * 60 || utcOffsetMinutes > 14 * 60) {
      USER_LOG_ERROR("Invalid %s value: %s",
                     AIRCRAFT_TIME_UTC_OFFSET_MINUTES_ENV, utcOffsetText);
      return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
  }
  s_aircraftTimeUtcOffsetMinutes = (int)utcOffsetMinutes;

  if (fcTransportDelayText != NULL) {
    errno = 0;
    endPtr = NULL;
    fcTransportDelayUs = strtoull(fcTransportDelayText, &endPtr, 10);
    if (errno != 0 || endPtr == fcTransportDelayText || *endPtr != '\0' ||
        fcTransportDelayUs > 1000000ULL) {
      USER_LOG_ERROR("Invalid %s value: %s", FC_TIME_TRANSPORT_DELAY_US_ENV,
                     fcTransportDelayText);
      return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
  }
  s_fcTransportDelayUs = (uint64_t)fcTransportDelayUs;

  pthread_mutex_lock(&s_fcClockSampleMutex);
  memset(s_fcClockSamples, 0, sizeof(s_fcClockSamples));
  s_fcClockSampleWriteIndex = 0;
  s_fcClockSampleSize = 0;
  s_lastFlightControllerTimeUs = 0;
  pthread_mutex_unlock(&s_fcClockSampleMutex);

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

  result = fcntl(request.fd, F_GETFL, 0);
  if (result < 0 || fcntl(request.fd, F_SETFL, result | O_NONBLOCK) < 0) {
    USER_LOG_ERROR("Set PPS GPIO event fd nonblocking failed, errno=%d", errno);
    close(request.fd);
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
  USER_LOG_INFO(
      "PPS time sync bridge initialized: %s line %ld, aircraft UTC offset %d min, FC transport compensation %llu us",
      gpioChip, gpioLine, s_aircraftTimeUtcOffsetMinutes,
      (unsigned long long)s_fcTransportDelayUs);
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
  uint64_t lastPpsLocalTimeUs;
  uint64_t currentLocalTimeUs;

  if (!s_timeSyncReady.load() || !s_ppsValid.load()) {
    return false;
  }

  lastPpsLocalTimeUs =
      s_lastPpsLocalTimeUs.load(std::memory_order_acquire);
  currentLocalTimeUs = GetMonotonicTimeUs();
  return lastPpsLocalTimeUs != 0 &&
         currentLocalTimeUs >= lastPpsLocalTimeUs &&
         currentLocalTimeUs - lastPpsLocalTimeUs <=
             PPS_FRESHNESS_TIMEOUT_US;
}

T_DjiReturnCode
TimeSyncBridge_GetNewestPpsLocalTimeUs(uint64_t *localTimeUs) {
  return GetNewestPpsTriggerLocalTimeUs(localTimeUs);
}

T_DjiReturnCode
TimeSyncBridge_ConvertLocalTimeUs(uint64_t localTimeUs,
                                  T_TimeSyncBridgeRealTime *realTime) {
  T_DjiTimeSyncAircraftTime aircraftTime;
  struct tm utcTime;
  time_t epochSeconds;
  T_DjiReturnCode returnCode;

  if (localTimeUs == 0 || realTime == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  memset(realTime, 0, sizeof(T_TimeSyncBridgeRealTime));
  if (!TimeSyncBridge_IsReady()) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  returnCode = DjiTimeSync_TransferToAircraftTime(localTimeUs, &aircraftTime);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    return returnCode;
  }

  if (!BuildEpochUs(&aircraftTime, s_aircraftTimeUtcOffsetMinutes,
                    &realTime->epochUs)) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  epochSeconds = (time_t)(realTime->epochUs / 1000000ULL);
  if (gmtime_r(&epochSeconds, &utcTime) == NULL) {
    memset(realTime, 0, sizeof(T_TimeSyncBridgeRealTime));
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  realTime->year = (uint16_t)(utcTime.tm_year + 1900);
  realTime->month = (uint8_t)(utcTime.tm_mon + 1);
  realTime->day = (uint8_t)utcTime.tm_mday;
  realTime->hour = (uint8_t)utcTime.tm_hour;
  realTime->minute = (uint8_t)utcTime.tm_min;
  realTime->second = (uint8_t)utcTime.tm_sec;
  realTime->microsecond = aircraftTime.microsecond;
  realTime->valid = true;

  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode
TimeSyncBridge_ConvertDataTimestamp(const T_DjiDataTimestamp *timestamp,
                                    T_TimeSyncBridgeRealTime *realTime) {
  uint64_t localTimeUs;

  if (timestamp == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  localTimeUs = (uint64_t)timestamp->millisecond * 1000ULL +
                (uint64_t)timestamp->microsecond;
  return TimeSyncBridge_ConvertLocalTimeUs(localTimeUs, realTime);
}

void TimeSyncBridge_RecordFcTimestamp(
    const T_DjiDataTimestamp *flightControllerTimestamp) {
  uint64_t flightControllerTimeUs;
  uint64_t localReceiveTimeUs;

  if (flightControllerTimestamp == NULL) {
    return;
  }

  flightControllerTimeUs =
      (uint64_t)flightControllerTimestamp->millisecond * 1000ULL +
      (uint64_t)flightControllerTimestamp->microsecond;
  if (flightControllerTimeUs == 0) {
    return;
  }

  localReceiveTimeUs = GetMonotonicTimeUs();
  if (pthread_mutex_trylock(&s_fcClockSampleMutex) != 0) {
    return;
  }

  if (s_lastFlightControllerTimeUs != 0 &&
      flightControllerTimeUs + 1000000ULL <
          s_lastFlightControllerTimeUs) {
    memset(s_fcClockSamples, 0, sizeof(s_fcClockSamples));
    s_fcClockSampleWriteIndex = 0;
    s_fcClockSampleSize = 0;
  }

  s_fcClockSamples[s_fcClockSampleWriteIndex].flightControllerTimeUs =
      flightControllerTimeUs;
  s_fcClockSamples[s_fcClockSampleWriteIndex].localReceiveTimeUs =
      localReceiveTimeUs;
  s_fcClockSampleWriteIndex =
      (s_fcClockSampleWriteIndex + 1) % FC_CLOCK_SAMPLE_COUNT;
  if (s_fcClockSampleSize < FC_CLOCK_SAMPLE_COUNT) {
    ++s_fcClockSampleSize;
  }
  s_lastFlightControllerTimeUs = flightControllerTimeUs;
  pthread_mutex_unlock(&s_fcClockSampleMutex);
}

T_DjiReturnCode TimeSyncBridge_ConvertFcTimestamp(
    const T_DjiDataTimestamp *flightControllerTimestamp,
    T_TimeSyncBridgeRealTime *realTime) {
  uint64_t flightControllerTimeUs;
  int64_t minimumOffsetUs = INT64_MAX;
  int64_t estimatedLocalTimeSignedUs;
  uint64_t estimatedLocalTimeUs;
  size_t sampleIndex;

  if (flightControllerTimestamp == NULL || realTime == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  flightControllerTimeUs =
      (uint64_t)flightControllerTimestamp->millisecond * 1000ULL +
      (uint64_t)flightControllerTimestamp->microsecond;
  if (flightControllerTimeUs == 0) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  pthread_mutex_lock(&s_fcClockSampleMutex);
  if (s_fcClockSampleSize < FC_CLOCK_MIN_VALID_SAMPLES) {
    pthread_mutex_unlock(&s_fcClockSampleMutex);
    return DJI_ERROR_SYSTEM_MODULE_CODE_BUSY;
  }

  for (sampleIndex = 0; sampleIndex < s_fcClockSampleSize; ++sampleIndex) {
    const T_FcClockSample *sample = &s_fcClockSamples[sampleIndex];
    int64_t offsetUs;

    if (sample->flightControllerTimeUs == 0 ||
        sample->flightControllerTimeUs > (uint64_t)INT64_MAX ||
        sample->localReceiveTimeUs > (uint64_t)INT64_MAX) {
      continue;
    }
    offsetUs = (int64_t)sample->localReceiveTimeUs -
               (int64_t)sample->flightControllerTimeUs;
    if (offsetUs < minimumOffsetUs) {
      minimumOffsetUs = offsetUs;
    }
  }
  pthread_mutex_unlock(&s_fcClockSampleMutex);

  if (minimumOffsetUs == INT64_MAX ||
      flightControllerTimeUs > (uint64_t)INT64_MAX) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  if (minimumOffsetUs > 0 &&
      flightControllerTimeUs >
          (uint64_t)(INT64_MAX - minimumOffsetUs)) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }
  estimatedLocalTimeSignedUs =
      (int64_t)flightControllerTimeUs + minimumOffsetUs;
  if (estimatedLocalTimeSignedUs <= 0 ||
      s_fcTransportDelayUs >= (uint64_t)estimatedLocalTimeSignedUs) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }
  estimatedLocalTimeUs = (uint64_t)estimatedLocalTimeSignedUs;
  estimatedLocalTimeUs -= s_fcTransportDelayUs;
  return TimeSyncBridge_ConvertLocalTimeUs(estimatedLocalTimeUs, realTime);
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
  struct sched_param scheduleParameter;
  (void)arg;

  memset(&scheduleParameter, 0, sizeof(scheduleParameter));
  scheduleParameter.sched_priority = PPS_CAPTURE_THREAD_PRIORITY;
  if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &scheduleParameter) !=
      0) {
    USER_LOG_WARN(
        "Unable to set PPS capture thread real-time priority; run as root");
  }

  while (s_threadRunning.load()) {
    struct pollfd pollFd;
    int fd = s_eventFd.load();
    int pollResult;

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

    DrainPendingPpsEvents();
  }

  s_ppsValid.store(false, std::memory_order_release);
  return NULL;
}

static void DrainPendingPpsEvents(void) {
  struct gpioevent_data eventData;
  int fd = s_eventFd.load();

  if (fd < 0) {
    return;
  }

  pthread_mutex_lock(&s_ppsEventReadMutex);
  while (true) {
    ssize_t readSize = read(fd, &eventData, sizeof(eventData));
    if (readSize == (ssize_t)sizeof(eventData)) {
      RecordPpsEventTimestamp(eventData.timestamp);
      continue;
    }

    if (readSize < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
        errno != EINTR && s_threadRunning.load()) {
      USER_LOG_ERROR("Read PPS GPIO event failed, errno=%d", errno);
    }
    break;
  }
  pthread_mutex_unlock(&s_ppsEventReadMutex);
}

static void RecordPpsEventTimestamp(uint64_t eventTimestampNs) {
  uint64_t localTimeUs = eventTimestampNs / 1000ULL;
  uint64_t previousLocalTimeUs;

  if (localTimeUs == 0) {
    localTimeUs = GetMonotonicTimeUs();
  }

  previousLocalTimeUs =
      s_lastPpsLocalTimeUs.load(std::memory_order_acquire);
  if (previousLocalTimeUs != 0 &&
      (localTimeUs <= previousLocalTimeUs ||
       localTimeUs - previousLocalTimeUs < PPS_DEGLITCH_INTERVAL_US)) {
    return;
  }

  s_lastPpsLocalTimeUs.store(localTimeUs, std::memory_order_release);
  s_ppsValid.store(true, std::memory_order_release);
}

static T_DjiReturnCode GetNewestPpsTriggerLocalTimeUs(uint64_t *localTimeUs) {
  if (localTimeUs == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  // The aircraft timestamp packet may race the userspace capture thread.
  // Drain the kernel event queue here so the SDK always receives the newest
  // hardware-timestamped PPS edge instead of the previous second's edge.
  DrainPendingPpsEvents();

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
                         int utcOffsetMinutes, uint64_t *epochUs) {
  struct tm utcTime;
  time_t epochSec;
  int64_t adjustedEpochSec;

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

  adjustedEpochSec =
      (int64_t)epochSec - (int64_t)utcOffsetMinutes * 60LL;
  if (adjustedEpochSec < 0) {
    return false;
  }

  *epochUs =
      (uint64_t)adjustedEpochSec * 1000000ULL + aircraftTime->microsecond;
  return true;
}
