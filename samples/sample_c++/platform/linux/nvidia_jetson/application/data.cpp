/**
 ********************************************************************
 * @file    data.cpp
 * @brief   数据订阅封装模块实现
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 *********************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "data.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <dji_logger.h>
#include <dji_platform.h>

/* Private constants ---------------------------------------------------------*/
#define RAD_TO_DEG 57.295779513082320876798154814105f

/* Private variables ---------------------------------------------------------*/
static std::atomic<bool> s_hasLoggedNoGlobalPosition(false);

/* Private functions declaration ---------------------------------------------*/
static void QuaternionToEuler(const T_DjiFcSubscriptionQuaternion *q,
                              dji_f32_t *pitch, dji_f32_t *roll,
                              dji_f32_t *yaw);
static bool IsValidGlobalCoordinate(dji_f64_t latitude, dji_f64_t longitude);

/* Exported functions definition ---------------------------------------------*/
T_DjiReturnCode DataSubscriber_Init(void) {
  T_DjiReturnCode returnCode;

  s_hasLoggedNoGlobalPosition.store(false);

  returnCode = DjiFcSubscription_Init();
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Init FC subscription module failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  USER_LOG_INFO("FC subscription module initialized successfully");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode DataSubscriber_DeInit(void) {
  T_DjiReturnCode returnCode;

  s_hasLoggedNoGlobalPosition.store(false);

  returnCode = DjiFcSubscription_DeInit();
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("DeInit FC subscription module failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  USER_LOG_INFO("FC subscription module deinitialized successfully");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode DataSubscriber_SubscribeTopics(void) {
  T_DjiReturnCode returnCode;

  // 订阅四元数 - 50Hz
  returnCode =
      DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION,
                                       DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Subscribe quaternion topic failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  // 订阅GPS位置 - 5Hz
  returnCode =
      DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_GPS_POSITION,
                                       DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Subscribe GPS position topic failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  // 订阅云台角度 - 50Hz
  returnCode =
      DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES,
                                       DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Subscribe gimbal angles topic failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  // 订阅融合位置 - 10Hz
  returnCode =
      DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
                                       DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR(
        "Subscribe position fused topic failed, error code: 0x%08llX",
        returnCode);
    return returnCode;
  }

  // 订阅速度 - 10Hz
  returnCode =
      DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY,
                                       DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Subscribe velocity topic failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  // 订阅融合高度 - 10Hz
  returnCode =
      DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_HEIGHT_FUSION,
                                       DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Subscribe height fusion topic failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  // 订阅飞行状态 - 10Hz
  returnCode =
      DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT,
                                       DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Subscribe flight status topic failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  // 订阅显示模式 - 10Hz
  returnCode = DjiFcSubscription_SubscribeTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_STATUS_DISPLAYMODE,
      DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Subscribe display mode topic failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  // 订阅电池信息 - 1Hz
  returnCode =
      DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO,
                                       DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Subscribe battery info topic failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  // 订阅GPS详细信息 - 5Hz
  returnCode =
      DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_GPS_DETAILS,
                                       DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Subscribe GPS details topic failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  // 订阅控制设备信息 - 5Hz
  returnCode =
      DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_CONTROL_DEVICE,
                                       DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR(
        "Subscribe control device topic failed, error code: 0x%08llX",
        returnCode);
    return returnCode;
  }

  // 订阅返航点状态 - 1Hz
  returnCode = DjiFcSubscription_SubscribeTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_SET_STATUS,
      DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR(
        "Subscribe home point status topic failed, error code: 0x%08llX",
        returnCode);
    return returnCode;
  }

  // 订阅RTK位置 - 5Hz
  returnCode =
      DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_RTK_POSITION,
                                       DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Subscribe RTK position topic failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  // 订阅RTK状态 (Position Info) - 1Hz
  // Note: Position Info contains the fix status (e.g. FIXED/FLOAT/NONE)
  returnCode = DjiFcSubscription_SubscribeTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_RTK_POSITION_INFO,
      DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ, NULL);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Subscribe RTK info topic failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  USER_LOG_INFO("All topics subscribed successfully");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode DataSubscriber_GetDroneStatus(T_DroneStatus *status) {
  T_DjiReturnCode returnCode;
  T_DjiDataTimestamp timestamp;
  T_DjiDataTimestamp gpsPositionTimestamp = {0};
  T_DjiDataTimestamp gpsDetailsTimestamp = {0};
  T_DjiDataTimestamp rtkPositionTimestamp = {0};
  T_DjiDataTimestamp rtkInfoTimestamp = {0};
  T_DjiReturnCode gpsPositionRet;
  T_DjiReturnCode gpsDetailsRet;
  T_DjiReturnCode rtkPositionRet;
  T_DjiReturnCode rtkInfoRet;
  dji_f64_t gpsLatitudeDeg;
  dji_f64_t gpsLongitudeDeg;
  bool hasGpsFix;
  bool gpsValid;
  bool rtkAvailable;

  if (status == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  memset(status, 0, sizeof(T_DroneStatus));
  status->positionSource = DRONE_POSITION_SOURCE_UNAVAILABLE;

  // 获取四元数
  returnCode = DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION, (uint8_t *)&status->quaternion,
      sizeof(T_DjiFcSubscriptionQuaternion), &timestamp);
  if (returnCode == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    QuaternionToEuler(&status->quaternion, &status->pitch, &status->roll,
                      &status->yaw);
  }

  // 获取GPS位置
  gpsPositionRet = DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_GPS_POSITION, (uint8_t *)&status->gpsPosition,
      sizeof(T_DjiFcSubscriptionGpsPosition), &gpsPositionTimestamp);
  if (gpsPositionRet == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    status->gpsPositionTimestamp = gpsPositionTimestamp;
  }

  // 获取RTK位置和状态
  rtkPositionRet = DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_RTK_POSITION, (uint8_t *)&status->rtkPosition,
      sizeof(T_DjiFcSubscriptionRtkPosition), &rtkPositionTimestamp);
  if (rtkPositionRet == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    status->rtkPositionTimestamp = rtkPositionTimestamp;
  }
  rtkInfoRet = DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_RTK_POSITION_INFO, (uint8_t *)&status->rtkPositionInfo,
      sizeof(T_DjiFcSubscriptionRtkPositionInfo), &rtkInfoTimestamp);
  if (rtkInfoRet == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    status->rtkInfoTimestamp = rtkInfoTimestamp;
  }

  // 获取GPS详情，用于判断GPS是否可作为降级全局位置源
  gpsDetailsRet = DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_GPS_DETAILS, (uint8_t *)&status->gpsDetails,
      sizeof(T_DjiFcSubscriptionGpsDetails), &gpsDetailsTimestamp);
  if (gpsDetailsRet == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    status->gpsDetailsTimestamp = gpsDetailsTimestamp;
  }

  rtkAvailable = (rtkPositionRet == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) &&
                 (rtkInfoRet == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) &&
                 (status->rtkPositionInfo != 0) &&
                 IsValidGlobalCoordinate(status->rtkPosition.latitude,
                                         status->rtkPosition.longitude);

  gpsLatitudeDeg = (dji_f64_t)status->gpsPosition.y / 10000000.0;
  gpsLongitudeDeg = (dji_f64_t)status->gpsPosition.x / 10000000.0;
  hasGpsFix = status->gpsDetails.fixState ==
                  DJI_FC_SUBSCRIPTION_GPS_FIX_STATE_3D_FIX ||
              status->gpsDetails.fixState ==
                  DJI_FC_SUBSCRIPTION_GPS_FIX_STATE_GPS_PLUS_DEAD_RECKONING;
  gpsValid = (gpsPositionRet == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) &&
             (gpsDetailsRet == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) &&
             hasGpsFix &&
             IsValidGlobalCoordinate(gpsLatitudeDeg, gpsLongitudeDeg);

  if (rtkAvailable) {
    status->latitude = status->rtkPosition.latitude;
    status->longitude = status->rtkPosition.longitude;
    status->altitude = status->rtkPosition.hfsl;
    status->positionSource = DRONE_POSITION_SOURCE_RTK_AVAILABLE;
  } else if (gpsValid) {
    status->latitude = gpsLatitudeDeg;
    status->longitude = gpsLongitudeDeg;
    status->altitude = (dji_f32_t)status->gpsPosition.z / 1000.0f;
    status->positionSource = DRONE_POSITION_SOURCE_GPS;
  } else {
    bool expected = false;
    if (s_hasLoggedNoGlobalPosition.compare_exchange_strong(expected, true)) {
      USER_LOG_WARN("No valid global position source available. RTK info=%u, GPS fix=%.0f",
                    status->rtkPositionInfo, status->gpsDetails.fixState);
    }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
  }

  // 获取云台角度
  T_DjiFcSubscriptionGimbalAngles gimbalAngles = {0};
  returnCode = DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES, (uint8_t *)&gimbalAngles,
      sizeof(T_DjiFcSubscriptionGimbalAngles), &timestamp);
  if (returnCode == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    status->gimbalPitch = gimbalAngles.x;
    status->gimbalRoll = gimbalAngles.y;
    status->gimbalYaw = gimbalAngles.z;
  } else {
    status->gimbalPitch = 0.0f;
    status->gimbalRoll = 0.0f;
    status->gimbalYaw = 0.0f;
  }

  // 获取融合位置
  DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
      (uint8_t *)&status->positionFused,
      sizeof(T_DjiFcSubscriptionPositionFused), &timestamp);

  // 获取速度
  DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY, (uint8_t *)&status->velocity,
      sizeof(T_DjiFcSubscriptionVelocity), &timestamp);

  // 获取高度
  DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_HEIGHT_FUSION, (uint8_t *)&status->heightFusion,
      sizeof(T_DjiFcSubscriptionHeightFusion), &timestamp);

  // 获取飞行状态
  DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT, (uint8_t *)&status->flightStatus,
      sizeof(T_DjiFcSubscriptionFlightStatus), &timestamp);

  // 获取显示模式
  DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_STATUS_DISPLAYMODE,
      (uint8_t *)&status->displayMode, sizeof(T_DjiFcSubscriptionDisplaymode),
      &timestamp);

  // 获取电池信息
  DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO, (uint8_t *)&status->batteryInfo,
      sizeof(T_DjiFcSubscriptionWholeBatteryInfo), &timestamp);

  // 获取控制设备
  DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_CONTROL_DEVICE,
      (uint8_t *)&status->controlDevice,
      sizeof(T_DjiFcSubscriptionControlDevice), &timestamp);

  // 获取返航点状态
  DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_HOME_POINT_SET_STATUS,
      (uint8_t *)&status->homePointSetStatus,
      sizeof(T_DjiFcSubscriptionHomePointSetStatus), &timestamp);

  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode
DataSubscriber_GetQuaternion(T_DjiFcSubscriptionQuaternion *quaternion) {
  T_DjiDataTimestamp timestamp;

  if (quaternion == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  return DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION, (uint8_t *)quaternion,
      sizeof(T_DjiFcSubscriptionQuaternion), &timestamp);
}

T_DjiReturnCode DataSubscriber_GetEulerAngles(dji_f32_t *pitch, dji_f32_t *roll,
                                              dji_f32_t *yaw) {
  T_DjiReturnCode returnCode;
  T_DjiFcSubscriptionQuaternion quaternion;
  T_DjiDataTimestamp timestamp;

  if (pitch == NULL || roll == NULL || yaw == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  returnCode = DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION, (uint8_t *)&quaternion,
      sizeof(T_DjiFcSubscriptionQuaternion), &timestamp);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    return returnCode;
  }

  QuaternionToEuler(&quaternion, pitch, roll, yaw);
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode
DataSubscriber_GetGpsPosition(T_DjiFcSubscriptionGpsPosition *gpsPosition) {
  T_DjiDataTimestamp timestamp;

  if (gpsPosition == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  return DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_GPS_POSITION, (uint8_t *)gpsPosition,
      sizeof(T_DjiFcSubscriptionGpsPosition), &timestamp);
}

T_DjiReturnCode
DataSubscriber_GetPositionFused(T_DjiFcSubscriptionPositionFused *position) {
  T_DjiDataTimestamp timestamp;

  if (position == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  return DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED, (uint8_t *)position,
      sizeof(T_DjiFcSubscriptionPositionFused), &timestamp);
}

T_DjiReturnCode
DataSubscriber_GetVelocity(T_DjiFcSubscriptionVelocity *velocity) {
  T_DjiDataTimestamp timestamp;

  if (velocity == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  return DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY, (uint8_t *)velocity,
      sizeof(T_DjiFcSubscriptionVelocity), &timestamp);
}

T_DjiReturnCode
DataSubscriber_GetFlightStatus(T_DjiFcSubscriptionFlightStatus *flightStatus) {
  T_DjiDataTimestamp timestamp;

  if (flightStatus == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  return DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT, (uint8_t *)flightStatus,
      sizeof(T_DjiFcSubscriptionFlightStatus), &timestamp);
}

T_DjiReturnCode DataSubscriber_GetBatteryInfo(
    T_DjiFcSubscriptionWholeBatteryInfo *batteryInfo) {
  T_DjiDataTimestamp timestamp;

  if (batteryInfo == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  return DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_BATTERY_INFO, (uint8_t *)batteryInfo,
      sizeof(T_DjiFcSubscriptionWholeBatteryInfo), &timestamp);
}

T_DjiReturnCode DataSubscriber_GetAltitude(dji_f32_t *altitude) {
  T_DjiReturnCode returnCode;
  T_DjiFcSubscriptionHeightFusion heightFusion;
  T_DjiDataTimestamp timestamp;

  if (altitude == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  returnCode = DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_HEIGHT_FUSION, (uint8_t *)&heightFusion,
      sizeof(T_DjiFcSubscriptionHeightFusion), &timestamp);
  if (returnCode == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    *altitude = heightFusion;
  }

  return returnCode;
}

T_DjiReturnCode
DataSubscriber_GetRtkPosition(T_DjiFcSubscriptionRtkPosition *rtkPosition) {
  T_DjiDataTimestamp timestamp;

  if (rtkPosition == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  return DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_RTK_POSITION, (uint8_t *)rtkPosition,
      sizeof(T_DjiFcSubscriptionRtkPosition), &timestamp);
}

T_DjiReturnCode DataSubscriber_GetRtkPositionInfo(
    T_DjiFcSubscriptionRtkPositionInfo *rtkPositionInfo) {
  T_DjiDataTimestamp timestamp;

  if (rtkPositionInfo == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  return DjiFcSubscription_GetLatestValueOfTopic(
      DJI_FC_SUBSCRIPTION_TOPIC_RTK_POSITION_INFO, (uint8_t *)rtkPositionInfo,
      sizeof(T_DjiFcSubscriptionRtkPositionInfo), &timestamp);
}

void DataSubscriber_PrintStatus(void) {
  T_DroneStatus status;
  T_DjiReturnCode returnCode;

  returnCode = DataSubscriber_GetDroneStatus(&status);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    printf("Failed to get drone status\n");
    return;
  }

  printf("\n================== 无人机状态 ==================\n");
  printf("【姿态信息】\n");
  printf("  俯仰角 (Pitch): %.2f°\n", status.pitch);
  printf("  横滚角 (Roll):  %.2f°\n", status.roll);
  printf("  偏航角 (Yaw):   %.2f°\n", status.yaw);

  printf("\n【云台信息】\n");
  printf("  俯仰角 (Pitch): %.2f°\n", status.gimbalPitch);
  printf("  横滚角 (Roll):  %.2f°\n", status.gimbalRoll);
  printf("  偏航角 (Yaw):   %.2f°\n", status.gimbalYaw);

  printf("\n【位置信息】\n");
  
  printf("  当前主定位源: %s\n",
         DataSubscriber_GetPositionSourceString(status.positionSource));
  printf("  RTK 解算状态: %u (0=完全无解,16=单点解,34=浮点解Float,50=固定解Fixed)\n", status.rtkPositionInfo);
  printf("  GPS Fix 状态: %.0f\n", status.gpsDetails.fixState);
  printf("  纬度: %.7f°\n", status.latitude);
  printf("  经度: %.7f°\n", status.longitude);
  printf("  绝对高度: %.2f m\n", status.altitude);
  printf("  融合高度: %.2f m\n", status.heightFusion);
  printf("  卫星数量: %u\n", status.positionFused.visibleSatelliteNumber);

  printf("\n【速度信息】\n");
  printf("  Vx: %.2f m/s\n", status.velocity.data.x);
  printf("  Vy: %.2f m/s\n", status.velocity.data.y);
  printf("  Vz: %.2f m/s\n", status.velocity.data.z);

  printf("\n【飞行状态】\n");
  printf("  飞行状态: %s\n",
         DataSubscriber_GetFlightStatusString(status.flightStatus));
  printf("  显示模式: %s\n",
         DataSubscriber_GetDisplayModeString(status.displayMode));

  printf("\n【电池信息】\n");
  printf("  电压: %d mV\n", status.batteryInfo.voltage);
  printf("  电流: %d mA\n", status.batteryInfo.current);
  printf("  电量: %d%%\n", status.batteryInfo.percentage);
  printf("  容量: %u mAh\n", status.batteryInfo.capacity);

  printf("\n【返航点】\n");
  printf("  返航点设置: %s\n",
         status.homePointSetStatus ==
                 DJI_FC_SUBSCRIPTION_HOME_POINT_SET_STATUS_SUCCESS
             ? "已设置"
             : "未设置");

  printf("================================================\n");
}

const char *
DataSubscriber_GetPositionSourceString(E_DronePositionSource source) {
  switch (source) {
  case DRONE_POSITION_SOURCE_RTK_AVAILABLE:
    return "RTK Available";
  case DRONE_POSITION_SOURCE_GPS:
    return "GPS";
  case DRONE_POSITION_SOURCE_UNAVAILABLE:
  default:
    return "Unavailable";
  }
}

const char *
DataSubscriber_GetFlightStatusString(T_DjiFcSubscriptionFlightStatus status) {
  switch (status) {
  case DJI_FC_SUBSCRIPTION_FLIGHT_STATUS_STOPED:
    return "停止 (电机静止)";
  case DJI_FC_SUBSCRIPTION_FLIGHT_STATUS_ON_GROUND:
    return "地面 (电机旋转)";
  case DJI_FC_SUBSCRIPTION_FLIGHT_STATUS_IN_AIR:
    return "空中飞行";
  default:
    return "未知状态";
  }
}

static bool IsValidGlobalCoordinate(dji_f64_t latitude, dji_f64_t longitude) {
  if (std::abs(latitude) < 0.000001 && std::abs(longitude) < 0.000001) {
    return false;
  }

  if (latitude < -90.0 || latitude > 90.0) {
    return false;
  }

  if (longitude < -180.0 || longitude > 180.0) {
    return false;
  }

  return true;
}

const char *
DataSubscriber_GetDisplayModeString(T_DjiFcSubscriptionDisplaymode mode) {
  switch (mode) {
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_MANUAL_CTRL:
    return "手动控制";
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_ATTITUDE:
    return "姿态模式";
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_P_GPS:
    return "GPS模式";
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_HOTPOINT_MODE:
    return "热点模式";
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_ASSISTED_TAKEOFF:
    return "辅助起飞";
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_AUTO_TAKEOFF:
    return "自动起飞";
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_AUTO_LANDING:
    return "自动降落";
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_NAVI_GO_HOME:
    return "自动返航";
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_NAVI_SDK_CTRL:
    return "SDK控制";
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_FORCE_AUTO_LANDING:
    return "强制降落";
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_SEARCH_MODE:
    return "搜索模式";
  case DJI_FC_SUBSCRIPTION_DISPLAY_MODE_ENGINE_START:
    return "电机启动";
  default:
    return "未知模式";
  }
}

/* Private functions definition-----------------------------------------------*/
static void QuaternionToEuler(const T_DjiFcSubscriptionQuaternion *q,
                              dji_f32_t *pitch, dji_f32_t *roll,
                              dji_f32_t *yaw) {
  if (q == NULL || pitch == NULL || roll == NULL || yaw == NULL) {
    return;
  }

  // 四元数转欧拉角
  // pitch = arcsin(-2 * (q1*q3 - q0*q2))
  *pitch = asinf(-2.0f * (q->q1 * q->q3 - q->q0 * q->q2)) * RAD_TO_DEG;

  // roll = atan2(2 * (q2*q3 + q0*q1), 1 - 2*(q1^2 + q2^2))
  *roll = atan2f(2.0f * (q->q2 * q->q3 + q->q0 * q->q1),
                 1.0f - 2.0f * (q->q1 * q->q1 + q->q2 * q->q2)) *
          RAD_TO_DEG;

  // yaw = atan2(2 * (q1*q2 + q0*q3), 1 - 2*(q2^2 + q3^2))
  *yaw = atan2f(2.0f * (q->q1 * q->q2 + q->q0 * q->q3),
                1.0f - 2.0f * (q->q2 * q->q2 + q->q3 * q->q3)) *
         RAD_TO_DEG;
}

/************************ (C) COPYRIGHT DJI Innovations *****END OF FILE****/
