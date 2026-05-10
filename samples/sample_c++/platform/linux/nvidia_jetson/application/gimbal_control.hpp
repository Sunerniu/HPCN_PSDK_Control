/**
 * ********************************************************************
 * @file    gimbal_control.hpp
 * @brief   云台控制接口 (Gimbal Control Interface)
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 * *********************************************************************
 */

#ifndef GIMBAL_CONTROL_HPP
#define GIMBAL_CONTROL_HPP

/* Includes ------------------------------------------------------------------*/
#include <dji_gimbal_manager.h>
#include <dji_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported types ------------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化云台管理模块并设置云台模式
 * @return 执行结果
 */
T_DjiReturnCode GimbalControl_Init(void);

/**
 * @brief 反初始化云台管理模块
 * @return 执行结果
 */
T_DjiReturnCode GimbalControl_DeInit(void);

/**
 * @brief 旋转云台
 * @param pitch 俯仰角，单位：0.1 度
 * @param roll 横滚角，单位：0.1 度
 * @param yaw 偏航角，单位：0.1 度
 * @param time 动作时间，单位：秒 (将转换为0.1s内)
 * @return 执行结果
 */
T_DjiReturnCode GimbalControl_Rotate(float pitch, float roll, float yaw, float time);

/**
 * @brief 恢复云台初始位置 (重置云台)
 * @return 执行结果
 */
T_DjiReturnCode GimbalControl_Reset(void);

#ifdef __cplusplus
}
#endif

#endif // GIMBAL_CONTROL_HPP
