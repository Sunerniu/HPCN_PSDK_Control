/**
 ********************************************************************
 * @file    precise_positioning.hpp
 * @brief   Delayed precise positioning results tied to PPS event time.
 *********************************************************************
 */

#ifndef PRECISE_POSITIONING_HPP
#define PRECISE_POSITIONING_HPP

#include <dji_positioning.h>
#include <dji_typedef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool valid;
  uint64_t gpsPreciseTimeEpochUs;
  T_DjiPositioningPositionInfo positionInfo;
} T_PrecisePositioningResult;

T_DjiReturnCode PrecisePositioning_Init(void);
T_DjiReturnCode PrecisePositioning_DeInit(void);
bool PrecisePositioning_GetLatest(T_PrecisePositioningResult *result);

#ifdef __cplusplus
}
#endif

#endif // PRECISE_POSITIONING_HPP
