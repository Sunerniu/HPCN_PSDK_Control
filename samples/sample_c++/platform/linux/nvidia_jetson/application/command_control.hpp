/**
 * ********************************************************************
 * @file    command_control.hpp
 * @brief   飞行控制封装模块头文件 (精简版)
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 * *********************************************************************
 */

#ifndef COMMAND_CONTROL_HPP
#define COMMAND_CONTROL_HPP

/* Includes ------------------------------------------------------------------*/
#include <dji_flight_controller.h>
#include <dji_typedef.h>
#ifdef __cplusplus
#include <vector>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 飞行控制初始化参数
 */
typedef struct {
  dji_f64_t ridLatitude;  // RID纬度 (度)
  dji_f64_t ridLongitude; // RID经度 (度)
  uint16_t ridAltitude;   // RID高度 (米)
} T_CommandControlConfig;

/**
 * @brief 速度控制命令 (NED坐标系)
 */
typedef struct {
  dji_f32_t vx;      // 北向速度 (m/s)
  dji_f32_t vy;      // 东向速度 (m/s)
  dji_f32_t vz;      // 垂直速度 (m/s), 正向为上
  dji_f32_t yawRate; // 偏航角速度 (deg/s)
} T_VelocityCommand;

/**
 * @brief 航点结构体
 */
typedef struct {
  dji_f64_t latitude;  // 纬度 (度)
  dji_f64_t longitude; // 经度 (度)
  dji_f32_t altitude;  // 绝对高度 (米)
  dji_f32_t speed;     // 速度 (m/s)
  dji_f32_t heading;   // 航向 (度)
  uint16_t stayTime;   // 停留时间 (秒)
} T_Waypoint;

/**
 * @brief 导航状态
 */
typedef enum {
  NAV_STATUS_IDLE = 0,   // 空闲，无任务
  NAV_STATUS_RUNNING,    // 正在执行导航
  NAV_STATUS_STOPPED,    // 已暂停，可恢复
  NAV_STATUS_COMPLETED,  // 已完成
  NAV_STATUS_ERROR,      // 导航异常
} E_NavigationStatus;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化飞行控制模块
 * @param config 初始化配置参数
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_Init(const T_CommandControlConfig *config);

/**
 * @brief 反初始化飞行控制模块
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_DeInit(void);

/**
 * @brief 获取操纵杆控制权限
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_ObtainJoystickAuthority(void);

/**
 * @brief 释放操纵杆控制权限
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_ReleaseJoystickAuthority(void);

/**
 * @brief 起飞
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_Takeoff(void);

/**
 * @brief 降落
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_Land(void);

/**
 * @brief 确认降落 (当在0.7m高度悬停时使用)
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_ConfirmLanding(void);

/**
 * @brief 强制降落 (忽略智能降落检查)
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_ForceLanding(void);

/**
 * @brief 取消降落
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_CancelLanding(void);

/**
 * @brief 返航
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_GoHome(void);

/**
 * @brief 取消返航
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_CancelGoHome(void);

/**
 * @brief 执行速度控制 (NED坐标系)
 * @param cmd 速度控制命令
 * @return 执行结果
 */
T_DjiReturnCode
CommandControl_ExecuteVelocityControl(const T_VelocityCommand *cmd);

/**
 * @brief 悬停 (停止所有运动)
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_Hover(void);

/**
 * @brief 追加航点到队列 (连续导航)
 * @param waypoint 目标点信息
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_PlansTo(const T_Waypoint *waypoint);

/**
 * @brief 切换到新的目标点 (丢弃原目标/队列)
 * @param waypoint 目标点信息
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_ChangeTo(const T_Waypoint *waypoint);

/**
 * @brief 启动导航
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_StartNavigation(void);

/**
 * @brief 暂停导航 (保留当前目标和队列，可恢复)
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_StopNavigation(void);

/**
 * @brief 仅暂停导航状态，不额外发送悬停速度指令
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_StopNavigationOnly(void);

/**
 * @brief 恢复导航
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_ResumeNavigation(void);

/**
 * @brief 清空导航任务并悬停
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_ClearNavigation(void);

/**
 * @brief 导航状态信息 (用于UDP遥测)
 */
#define NAV_MAX_QUEUE_REPORT 10 // 最多报告10个队列中的航点

typedef struct {
  E_NavigationStatus status; // 当前导航状态
  bool isNavigating;        // 是否正在导航
  bool hasActiveTarget;     // 是否有当前目标
  T_DjiReturnCode lastErrorCode; // 最近一次导航错误码
  T_Waypoint currentTarget; // 当前目标点
  uint8_t queueCount;       // 队列中的航点数量
  T_Waypoint
      queue[NAV_MAX_QUEUE_REPORT]; // 队列中的航点 (最多NAV_MAX_QUEUE_REPORT个)
} T_NavState;

/**
 * @brief 获取当前导航状态
 * @param state 导航状态输出指针
 * @return 执行结果
 */
T_DjiReturnCode CommandControl_GetNavState(T_NavState *state);

/**
 * @brief 获取导航状态字符串
 * @param status 导航状态
 * @return 状态字符串
 */
const char *CommandControl_GetNavigationStatusString(E_NavigationStatus status);

#ifdef __cplusplus
}
#endif

#endif // COMMAND_CONTROL_HPP
