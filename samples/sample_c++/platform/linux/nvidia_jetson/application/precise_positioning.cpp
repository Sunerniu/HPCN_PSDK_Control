/**
 ********************************************************************
 * @file    precise_positioning.cpp
 * @brief   Delayed precise positioning results tied to PPS event time.
 *********************************************************************
 */

#include "precise_positioning.hpp"
#include "time_sync_bridge.hpp"

#include <atomic>
#include <cstring>
#include <dji_logger.h>
#include <dji_platform.h>
#include <dji_time_sync.h>
#include <mutex>
#include <thread>

#define PRECISE_POSITION_EVENT_DELAY_US 1200000ULL
#define PRECISE_POSITION_RETRY_INTERVAL_MS 100

static std::atomic<bool> s_running(false);
static std::thread s_workerThread;
static std::mutex s_resultMutex;
static T_PrecisePositioningResult s_latestResult;

static void PrecisePositioningWorker(void);

T_DjiReturnCode PrecisePositioning_Init(void) {
  T_DjiReturnCode returnCode;

  if (s_running.load()) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
  }

  returnCode = DjiPositioning_Init();
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Precise positioning init failed: 0x%08llX", returnCode);
    return returnCode;
  }

  DjiPositioning_SetTaskIndex(0);
  {
    std::lock_guard<std::mutex> lock(s_resultMutex);
    memset(&s_latestResult, 0, sizeof(s_latestResult));
  }

  s_running.store(true);
  try {
    s_workerThread = std::thread(PrecisePositioningWorker);
  } catch (...) {
    s_running.store(false);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  USER_LOG_INFO("Precise positioning worker initialized");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode PrecisePositioning_DeInit(void) {
  s_running.store(false);
  if (s_workerThread.joinable()) {
    s_workerThread.join();
  }

  std::lock_guard<std::mutex> lock(s_resultMutex);
  memset(&s_latestResult, 0, sizeof(s_latestResult));
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

bool PrecisePositioning_GetLatest(T_PrecisePositioningResult *result) {
  if (result == NULL) {
    return false;
  }

  std::lock_guard<std::mutex> lock(s_resultMutex);
  *result = s_latestResult;
  return result->valid;
}

static void PrecisePositioningWorker(void) {
  T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();
  uint64_t lastRequestedEventLocalTimeUs = 0;

  while (s_running.load()) {
    uint64_t newestPpsLocalTimeUs = 0;
    uint64_t eventLocalTimeUs;
    T_DjiPositioningEventInfo eventInfo = {};
    T_DjiPositioningPositionInfo positionInfo = {};
    T_TimeSyncBridgeRealTime realTime = {};
    T_DjiReturnCode returnCode;

    if (!TimeSyncBridge_IsReady() ||
        TimeSyncBridge_GetNewestPpsLocalTimeUs(&newestPpsLocalTimeUs) !=
            DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ||
        newestPpsLocalTimeUs <= PRECISE_POSITION_EVENT_DELAY_US) {
      osalHandler->TaskSleepMs(PRECISE_POSITION_RETRY_INTERVAL_MS);
      continue;
    }

    eventLocalTimeUs =
        newestPpsLocalTimeUs - PRECISE_POSITION_EVENT_DELAY_US;
    if (eventLocalTimeUs == lastRequestedEventLocalTimeUs) {
      osalHandler->TaskSleepMs(PRECISE_POSITION_RETRY_INTERVAL_MS);
      continue;
    }

    eventInfo.eventSetIndex = 0;
    eventInfo.targetPointIndex = 0;
    returnCode = DjiTimeSync_TransferToAircraftTime(eventLocalTimeUs,
                                                    &eventInfo.eventTime);
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
      osalHandler->TaskSleepMs(PRECISE_POSITION_RETRY_INTERVAL_MS);
      continue;
    }

    returnCode =
        DjiPositioning_GetPositionInformationSync(1, &eventInfo, &positionInfo);
    lastRequestedEventLocalTimeUs = eventLocalTimeUs;
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ||
        TimeSyncBridge_ConvertLocalTimeUs(eventLocalTimeUs, &realTime) !=
            DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ||
        !realTime.valid) {
      USER_LOG_WARN("Precise positioning query failed: 0x%08llX", returnCode);
      osalHandler->TaskSleepMs(PRECISE_POSITION_RETRY_INTERVAL_MS);
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(s_resultMutex);
      s_latestResult.valid = true;
      s_latestResult.gpsPreciseTimeEpochUs = realTime.epochUs;
      s_latestResult.positionInfo = positionInfo;
    }
  }
}
