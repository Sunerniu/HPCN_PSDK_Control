/**
 ********************************************************************
 * @file    data.hpp
 * @brief   数据订阅封装模块头文件
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 *********************************************************************
 */

#ifndef DATA_HPP
#define DATA_HPP

/* Includes ------------------------------------------------------------------*/
#include <dji_fc_subscription.h>
#include <dji_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 当前全局定位源
 */
typedef enum {
  DRONE_POSITION_SOURCE_UNAVAILABLE = 0,
  DRONE_POSITION_SOURCE_GPS = 1,
  DRONE_POSITION_SOURCE_RTK_AVAILABLE = 2,
} E_DronePositionSource;

/**
 * @brief 无人机完整状态信息结构体
 */
typedef struct {
  // 姿态信息
  T_DjiFcSubscriptionQuaternion quaternion;
  dji_f32_t pitch; // 俯仰角 (度)
  dji_f32_t roll;  // 横滚角 (度)
  dji_f32_t yaw;   // 偏航角 (度)

  // 云台角度信息
  dji_f32_t gimbalPitch; // 云台俯仰角 (度)
  dji_f32_t gimbalRoll;  // 云台横滚角 (度)
  dji_f32_t gimbalYaw;   // 云台偏航角 (度)

  // 位置信息
  T_DjiFcSubscriptionGpsPosition gpsPosition;
  T_DjiDataTimestamp gpsPositionTimestamp;
  T_DjiFcSubscriptionPositionFused positionFused;
  dji_f64_t latitude;  // 纬度 (度)
  dji_f64_t longitude; // 经度 (度)
  dji_f32_t altitude;  // 绝对高度 (米, WGS84高度源)
  E_DronePositionSource positionSource; // 当前使用的全局定位源

  // 速度信息
  T_DjiFcSubscriptionVelocity velocity;

  // 高度信息
  T_DjiFcSubscriptionHeightFusion heightFusion;
  T_DjiFcSubscriptionAltitudeOfHomePoint altitudeOfHomePoint;

  // 飞行状态
  T_DjiFcSubscriptionFlightStatus flightStatus;
  T_DjiFcSubscriptionDisplaymode displayMode;

  // 电池信息
  T_DjiFcSubscriptionWholeBatteryInfo batteryInfo;
  T_DjiFcSubscriptionSingleBatteryInfo battery1;
  T_DjiFcSubscriptionSingleBatteryInfo battery2;

  // GPS信息
  T_DjiFcSubscriptionGpsDetails gpsDetails;
  T_DjiDataTimestamp gpsDetailsTimestamp;
  T_DjiFcSubscriptionGpsSignalLevel gpsSignalLevel;

  // RTK 信息
  T_DjiFcSubscriptionRtkPosition rtkPosition;
  T_DjiDataTimestamp rtkPositionTimestamp;
  T_DjiFcSubscriptionRtkVelocity rtkVelocity;
  T_DjiFcSubscriptionRtkYaw rtkYaw;
  T_DjiFcSubscriptionRtkPositionInfo
      rtkPositionInfo; // Contains status (Fixed/Float)
  T_DjiDataTimestamp rtkInfoTimestamp;
  T_DjiFcSubscriptionRtkYawInfo rtkYawInfo;

  // 控制设备信息
  T_DjiFcSubscriptionControlDevice controlDevice;

  // 遥控器信息
  T_DjiFcSubscriptionRC rcData;

  // 返航点信息
  T_DjiFcSubscriptionHomePointSetStatus homePointSetStatus;
  T_DjiFcSubscriptionHomePointInfo homePointInfo;

} T_DroneStatus;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化数据订阅模块
 * @return 执行结果
 */
T_DjiReturnCode DataSubscriber_Init(void);

/**
 * @brief 反初始化数据订阅模块
 * @return 执行结果
 */
T_DjiReturnCode DataSubscriber_DeInit(void);

/**
 * @brief 订阅所有需要的数据主题
 * @return 执行结果
 */
T_DjiReturnCode DataSubscriber_SubscribeTopics(void);

/**
 * @brief 获取最新的无人机状态
 * @param status 状态信息输出指针
 * @return 执行结果
 */
T_DjiReturnCode DataSubscriber_GetDroneStatus(T_DroneStatus *status);

/**
 * @brief 获取四元数姿态
 * @param quaternion 输出指针
 * @return 执行结果
 */
T_DjiReturnCode
DataSubscriber_GetQuaternion(T_DjiFcSubscriptionQuaternion *quaternion);

/**
 * @brief 获取欧拉角姿态 (度)
 * @param pitch 俯仰角输出指针
 * @param roll 横滚角输出指针
 * @param yaw 偏航角输出指针
 * @return 执行结果
 */
T_DjiReturnCode DataSubscriber_GetEulerAngles(dji_f32_t *pitch, dji_f32_t *roll,
                                              dji_f32_t *yaw);

/**
 * @brief 获取GPS位置
 * @param gpsPosition 输出指针
 * @return 执行结果
 */
T_DjiReturnCode
DataSubscriber_GetGpsPosition(T_DjiFcSubscriptionGpsPosition *gpsPosition);

/**
 * @brief 获取融合位置
 * @param position 输出指针
 * @return 执行结果
 */
T_DjiReturnCode
DataSubscriber_GetPositionFused(T_DjiFcSubscriptionPositionFused *position);

/**
 * @brief 获取速度信息
 * @param velocity 输出指针
 * @return 执行结果
 */
T_DjiReturnCode
DataSubscriber_GetVelocity(T_DjiFcSubscriptionVelocity *velocity);

/**
 * @brief 获取飞行状态
 * @param flightStatus 输出指针
 * @return 执行结果
 */
T_DjiReturnCode
DataSubscriber_GetFlightStatus(T_DjiFcSubscriptionFlightStatus *flightStatus);

/**
 * @brief 获取电池信息
 * @param batteryInfo 输出指针
 * @return 执行结果
 */
T_DjiReturnCode
DataSubscriber_GetBatteryInfo(T_DjiFcSubscriptionWholeBatteryInfo *batteryInfo);

/**
 * @brief 获取RTK位置
 * @param rtkPosition 输出指针
 * @return 执行结果
 */
T_DjiReturnCode
DataSubscriber_GetRtkPosition(T_DjiFcSubscriptionRtkPosition *rtkPosition);

/**
 * @brief 获取RTK位置信息 (状态)
 * @param rtkPositionInfo 输出指针
 * @return 执行结果
 */
T_DjiReturnCode DataSubscriber_GetRtkPositionInfo(
    T_DjiFcSubscriptionRtkPositionInfo *rtkPositionInfo);

/**
 * @brief 获取高度信息
 * @param altitude 输出指针 (米)
 * @return 执行结果
 */
T_DjiReturnCode DataSubscriber_GetAltitude(dji_f32_t *altitude);

/**
 * @brief 打印当前无人机状态到控制台
 */
void DataSubscriber_PrintStatus(void);

/**
 * @brief 获取定位源字符串描述
 * @param source 定位源
 * @return 定位源描述字符串
 */
const char *DataSubscriber_GetPositionSourceString(E_DronePositionSource source);

/**
 * @brief 获取飞行状态字符串描述
 * @param status 飞行状态
 * @return 状态描述字符串
 */
const char *
DataSubscriber_GetFlightStatusString(T_DjiFcSubscriptionFlightStatus status);

/**
 * @brief 获取显示模式字符串描述
 * @param mode 显示模式
 * @return 模式描述字符串
 */
const char *
DataSubscriber_GetDisplayModeString(T_DjiFcSubscriptionDisplaymode mode);

#ifdef __cplusplus
}
#endif

#endif // DATA_HPP
