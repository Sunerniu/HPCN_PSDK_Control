/**
 ********************************************************************
 * @file    time_sync_bridge.hpp
 * @brief   PPS/GPIO time synchronization bridge for GPS/RTK timestamps.
 *********************************************************************
 */

#ifndef TIME_SYNC_BRIDGE_HPP
#define TIME_SYNC_BRIDGE_HPP

/* Includes ------------------------------------------------------------------*/
#include <dji_typedef.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported types ------------------------------------------------------------*/
typedef struct {
  bool valid;
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint32_t microsecond;
  uint64_t epochUs;
} T_TimeSyncBridgeRealTime;

/* Exported functions --------------------------------------------------------*/
T_DjiReturnCode TimeSyncBridge_Init(void);
T_DjiReturnCode TimeSyncBridge_DeInit(void);
bool TimeSyncBridge_IsReady(void);
T_DjiReturnCode
TimeSyncBridge_ConvertDataTimestamp(const T_DjiDataTimestamp *timestamp,
                                    T_TimeSyncBridgeRealTime *realTime);
void TimeSyncBridge_FormatIso8601(const T_TimeSyncBridgeRealTime *realTime,
                                  char *buffer, size_t bufferSize);

#ifdef __cplusplus
}
#endif

#endif // TIME_SYNC_BRIDGE_HPP
