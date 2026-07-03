/**
 ********************************************************************
 * @file    console_handler.cpp
 * @brief   命令处理模块实现 - 统一处理控制台和TCP命令
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 *********************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "console_handler.hpp"
#include "command_control.hpp"
#include "data.hpp"
#include "gimbal_control.hpp"

#include <dji_logger.h>
#include <dji_platform.h>

#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

/* Private variables ---------------------------------------------------------*/
static std::atomic<bool> s_isRunning(true);
static bool s_flightControlAvailable = false;

/* Private functions declaration ---------------------------------------------*/
static const char *GetSourceString(E_CommandSource source);
static T_DjiReturnCode ExecuteCommandInternal(E_CommandType cmdType,
                                              E_CommandSource source);
static T_DjiReturnCode ExecuteNavigationInternal(E_CommandType cmdType,
                                                 const T_Waypoint *wp,
                                                 E_CommandSource source);
static bool ParseWaypointInput(const std::string &input, T_Waypoint *wp);
static bool ValidateWaypoint(const T_Waypoint *wp, E_CommandSource source);

/* Private constants ---------------------------------------------------------*/
#define WAYPOINT_MAX_DISTANCE_M 2000.0 // 最大允许距离 (2000米)
#define WAYPOINT_MAX_ALTITUDE_M 1500.0  // 最大允许高度 (500米)
#define WAYPOINT_MIN_ALTITUDE_M 0.0    // 最小允许高度 (0米)
#define EARTH_RADIUS_M 6371000.0       // 地球半径 (米)

/* Exported functions definition ---------------------------------------------*/

T_DjiReturnCode ConsoleHandler_Init(const T_ConsoleConfig *config) {
  if (config != NULL) {
    s_flightControlAvailable = config->flightControlAvailable;
  }
  s_isRunning = true;

  USER_LOG_INFO("Console handler initialized");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

void ConsoleHandler_RequestStop(void) { s_isRunning = false; }

void ConsoleHandler_PrintHelp(void) {
  std::cout << "\n==================== 命令列表 ===================="
            << std::endl;
  std::cout << "【基础控制】" << std::endl;
  std::cout << "  takeoff, t       - 起飞" << std::endl;
  std::cout << "  land, l          - 降落" << std::endl;
  std::cout << "  home             - 返航" << std::endl;
  std::cout << "  cancelhome       - 取消返航" << std::endl;
  std::cout << "  hover            - 悬停" << std::endl;
  std::cout << "  confirmland      - 确认降落 (0.7m悬停时)" << std::endl;
  std::cout << "  forceland        - 强制降落" << std::endl;
  std::cout << "  cancelland       - 取消降落" << std::endl;
  std::cout << "  plansto <lat> <lon> [alt] [speed] - 追加航点并立即执行"
            << std::endl;
  std::cout << "  changesto <lat> <lon> [alt] [speed] - 切换目标并立即执行"
            << std::endl;
  std::cout << "  start            - 启动或继续导航" << std::endl;
  std::cout << "  stop             - 暂停导航，可恢复" << std::endl;
  std::cout << "  resume           - 恢复导航" << std::endl;
  std::cout << "  clear            - 清空全部导航任务" << std::endl;
  std::cout << "" << std::endl;
  std::cout << "【系统】" << std::endl;
  std::cout << "  auth             - 获取控制权限" << std::endl;
  std::cout << "  gimbal_rotate <p> <r> <y> [t] - 旋转云台(角度/时间)" << std::endl;
  std::cout << "  gimbal_reset     - 复位云台" << std::endl;
  std::cout << "  help, h          - 显示帮助" << std::endl;
  std::cout << "  quit, exit       - 退出程序" << std::endl;
  std::cout << "==================================================="
            << std::endl;
}

// ===== 命令执行接口 =====

T_DjiReturnCode ConsoleHandler_ExecuteCommand(E_CommandType cmdType,
                                              E_CommandSource source) {
  return ExecuteCommandInternal(cmdType, source);
}

T_DjiReturnCode
ConsoleHandler_ExecuteFromPacket(const T_ControlPacket *packet) {
  if (packet == NULL) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  uint8_t cmd = packet->cmdType;

  // 判断是否为导航命令 (0x10 - 0x2F)
  if ((cmd >= 0x10 && cmd <= 0x2F)) {
    T_Waypoint wp = {0};
    wp.latitude = packet->latitude;
    wp.longitude = packet->longitude;
    wp.altitude = packet->altitude;
    wp.speed = packet->speed;

    return ExecuteNavigationInternal((E_CommandType)cmd, &wp, CMD_SOURCE_TCP);
  }
  
  // 判断是否为云台控制命令 (0x40 - 0x4F)
  if (cmd == CMD_GIMBAL_ROTATE) {
    // 复用 latitude as pitch, longitude as roll, altitude as yaw, speed as time
    return GimbalControl_Rotate(packet->latitude, packet->longitude, packet->altitude, packet->speed);
  } else if (cmd == CMD_GIMBAL_RESET) {
    return GimbalControl_Reset();
  }

  // 否则作为基础命令执行
  return ExecuteCommandInternal((E_CommandType)cmd, CMD_SOURCE_TCP);
}

T_DjiReturnCode ConsoleHandler_ExecuteNavigation(E_CommandType cmdType,
                                                 const T_Waypoint *wp,
                                                 E_CommandSource source) {
  return ExecuteNavigationInternal(cmdType, wp, source);
}

// ===== 控制台交互循环 =====

void ConsoleHandler_RunLoop(void) {
  std::string input;

  ConsoleHandler_PrintHelp();

  while (s_isRunning) {
    std::cout << "\n>>> ";
    std::getline(std::cin, input);

    if (input.empty())
      continue;

    // ===== 基础控制 =====
    if (input == "help" || input == "h") {
      ConsoleHandler_PrintHelp();
    } else if (input == "takeoff" || input == "t") {
      if (!s_flightControlAvailable) {
        std::cout << "[ERROR] 飞控不可用" << std::endl;
      } else {
        ConsoleHandler_ExecuteCommand(CMD_TAKEOFF, CMD_SOURCE_CONSOLE);
      }
    } else if (input == "land" || input == "l") {
      if (!s_flightControlAvailable) {
        std::cout << "[ERROR] 飞控不可用" << std::endl;
      } else {
        ConsoleHandler_ExecuteCommand(CMD_LAND, CMD_SOURCE_CONSOLE);
      }
    } else if (input == "home" || input == "gohome") {
      if (!s_flightControlAvailable) {
        std::cout << "[ERROR] 飞控不可用" << std::endl;
      } else {
        ConsoleHandler_ExecuteCommand(CMD_GOHOME, CMD_SOURCE_CONSOLE);
      }
    } else if (input == "cancelhome") {
      if (s_flightControlAvailable) {
        ConsoleHandler_ExecuteCommand(CMD_CANCEL_GOHOME, CMD_SOURCE_CONSOLE);
      }
    } else if (input == "hover") {
      if (s_flightControlAvailable) {
        ConsoleHandler_ExecuteCommand(CMD_HOVER, CMD_SOURCE_CONSOLE);
      }
    } else if (input == "confirmland") {
      if (s_flightControlAvailable) {
        ConsoleHandler_ExecuteCommand(CMD_CONFIRM_LAND, CMD_SOURCE_CONSOLE);
      }
    } else if (input == "forceland") {
      if (s_flightControlAvailable) {
        ConsoleHandler_ExecuteCommand(CMD_FORCE_LAND, CMD_SOURCE_CONSOLE);
      }
    } else if (input == "cancelland") {
      if (s_flightControlAvailable) {
        ConsoleHandler_ExecuteCommand(CMD_CANCEL_LAND, CMD_SOURCE_CONSOLE);
      }
    } else if (input == "start") {
      ConsoleHandler_ExecuteNavigation(CMD_NAV_START, NULL, CMD_SOURCE_CONSOLE);
    } else if (input == "stop") {
      ConsoleHandler_ExecuteNavigation(CMD_NAV_STOP, NULL, CMD_SOURCE_CONSOLE);
    } else if (input == "resume") {
      ConsoleHandler_ExecuteNavigation(CMD_NAV_RESUME, NULL, CMD_SOURCE_CONSOLE);
    } else if (input == "clear") {
      ConsoleHandler_ExecuteNavigation(CMD_NAV_CLEAR, NULL, CMD_SOURCE_CONSOLE);
    } else if (input.find("plansto") == 0) {
      T_Waypoint wp = {0};
      if (ParseWaypointInput(input.substr(7), &wp)) {
        ConsoleHandler_ExecuteNavigation(CMD_PLANSTO, &wp, CMD_SOURCE_CONSOLE);
      } else {
        std::cout << "[ERROR] 参数错误，用法: plansto <lat> <lon> [绝对高度] [速度]"
                  << std::endl;
      }
    } else if (input.find("changesto") == 0) {
      T_Waypoint wp = {0};
      if (ParseWaypointInput(input.substr(9), &wp)) {
        ConsoleHandler_ExecuteNavigation(CMD_CHANGESTO, &wp,
                                         CMD_SOURCE_CONSOLE);
      } else {
        std::cout << "[ERROR] 参数错误，用法: changesto <lat> <lon> [绝对高度] [速度]"
                  << std::endl;
      }
    }

    // ===== 系统命令 =====
    else if (input == "status") {
      ConsoleHandler_ExecuteCommand(CMD_STATUS, CMD_SOURCE_CONSOLE);
    } else if (input == "auth") {
      if (s_flightControlAvailable) {
        ConsoleHandler_ExecuteCommand(CMD_AUTH, CMD_SOURCE_CONSOLE);
      }
    } else if (input.find("gimbal_rotate") == 0) {
      std::stringstream ss(input.substr(13));
      float p, r, y, t = 0.5;
      if (ss >> p >> r >> y) {
        if (ss >> t) {} // Optional time parameter
        GimbalControl_Rotate(p, r, y, t);
      } else {
        std::cout << "[ERROR] 参数错误，用法: gimbal_rotate <pitch> <roll> <yaw> [time]" << std::endl;
      }
    } else if (input == "gimbal_reset") {
      GimbalControl_Reset();
    } else if (input == "quit" || input == "exit" || input == "q") {
      std::cout << "[CMD] 退出..." << std::endl;
      s_isRunning = false;
    } else {
      std::cout << "[WARN] 未知命令: " << input << std::endl;
    }
  }
}

/* Private functions definition ----------------------------------------------*/

static T_DjiReturnCode ExecuteCommandInternal(E_CommandType cmdType,
                                              E_CommandSource source) {
  T_DjiReturnCode ret = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
  const char *srcStr = GetSourceString(source);

  switch (cmdType) {
  case CMD_TAKEOFF:
    std::cout << "[" << srcStr << "] 执行起飞" << std::endl;
    ret = CommandControl_Takeoff();
    break;

  case CMD_LAND:
    std::cout << "[" << srcStr << "] 执行降落" << std::endl;
    ret = CommandControl_Land();
    break;

  case CMD_GOHOME:
    std::cout << "[" << srcStr << "] 执行返航" << std::endl;
    ret = CommandControl_GoHome();
    break;

  case CMD_CANCEL_GOHOME:
    std::cout << "[" << srcStr << "] 取消返航" << std::endl;
    ret = CommandControl_CancelGoHome();
    break;

  case CMD_HOVER:
    std::cout << "[" << srcStr << "] 执行悬停" << std::endl;
    ret = CommandControl_Hover();
    break;

  case CMD_CONFIRM_LAND:
    std::cout << "[" << srcStr << "] 确认降落" << std::endl;
    ret = CommandControl_ConfirmLanding();
    break;

  case CMD_FORCE_LAND:
    std::cout << "[" << srcStr << "] 强制降落" << std::endl;
    ret = CommandControl_ForceLanding();
    break;

  case CMD_CANCEL_LAND:
    std::cout << "[" << srcStr << "] 取消降落" << std::endl;
    ret = CommandControl_CancelLanding();
    break;

  case CMD_STATUS:
    std::cout << "[" << srcStr << "] 打印状态" << std::endl;
    DataSubscriber_PrintStatus();
    break;

  case CMD_AUTH:
    std::cout << "[" << srcStr << "] 获取控制权限" << std::endl;
    ret = CommandControl_ObtainJoystickAuthority();
    break;

  case CMD_HEARTBEAT:
    break;

  default:
    std::cout << "[" << srcStr << "] 未知命令" << std::endl;
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  return ret;
}

static T_DjiReturnCode ExecuteNavigationInternal(E_CommandType cmdType,
                                                 const T_Waypoint *wp,
                                                 E_CommandSource source) {
  T_DjiReturnCode ret = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
  const char *srcStr = GetSourceString(source);

  switch (cmdType) {
  case CMD_NAV_START:
    std::cout << "[" << srcStr << "] 启动导航" << std::endl;
    ret = CommandControl_StartNavigation();
    break;

  case CMD_PLANSTO:
    if (wp) {
      // 位置合理性检查
      if (!ValidateWaypoint(wp, source)) {
        std::cout << "[" << srcStr << "] [SKIP] PlansTo 位置不合理，跳过: Lat "
                  << wp->latitude << ", Lon " << wp->longitude << ", Alt "
                  << wp->altitude << std::endl;
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
      }
      std::cout << "[" << srcStr << "] 追加PlansTo: Lat " << wp->latitude
                << ", Lon " << wp->longitude << std::endl;
      ret = CommandControl_PlansTo(wp);
    } else {
      ret = DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
    break;

  case CMD_CHANGESTO:
    if (wp) {
      // 位置合理性检查
      if (!ValidateWaypoint(wp, source)) {
        std::cout << "[" << srcStr
                  << "] [SKIP] ChangesTo 位置不合理，跳过: Lat " << wp->latitude
                  << ", Lon " << wp->longitude << ", Alt " << wp->altitude
                  << std::endl;
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
      }
      std::cout << "[" << srcStr << "] 立即切换ChangeTo: Lat " << wp->latitude
                << ", Lon " << wp->longitude << std::endl;
      ret = CommandControl_ChangeTo(wp);
    } else {
      ret = DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
    break;

  case CMD_NAV_STOP:
    std::cout << "[" << srcStr << "] 暂停导航" << std::endl;
    ret = CommandControl_StopNavigation();
    break;

  case CMD_NAV_RESUME:
    std::cout << "[" << srcStr << "] 恢复导航" << std::endl;
    ret = CommandControl_ResumeNavigation();
    break;

  case CMD_NAV_CLEAR:
    std::cout << "[" << srcStr << "] 清空导航任务" << std::endl;
    ret = CommandControl_ClearNavigation();
    break;

  default:
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT;
  }
  return ret;
}

static bool ParseWaypointInput(const std::string &input, T_Waypoint *wp) {
  if (wp == NULL)
    return false;
  std::stringstream ss(input);
  double lat, lon;
  float alt, spd;
  if (ss >> lat >> lon) {
    wp->latitude = lat;
    wp->longitude = lon;

    // Default values
    wp->speed = 0;
    wp->altitude = 0;

    if (ss >> alt) {
      wp->altitude = alt;
      if (ss >> spd) {
        wp->speed = spd;
      }
    } else {
      T_DroneStatus status = {};
      if (DataSubscriber_GetDroneStatus(&status) !=
          DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::cout << "[ERROR] 未提供高度，且无法获取当前高度" << std::endl;
        return false;
      }
      wp->altitude = status.altitude;
      std::cout << "[PARSER] 未提供高度，使用当前绝对高度: " << wp->altitude
                << " m" << std::endl;
    }

    std::cout << "[PARSER] Input Parsed: Lat=" << wp->latitude
              << ", Lon=" << wp->longitude << ", Alt=" << wp->altitude
              << ", Spd=" << wp->speed << std::endl;
    return true;
  }
  return false;
}

static const char *GetSourceString(E_CommandSource source) {
  switch (source) {
  case CMD_SOURCE_CONSOLE:
    return "CMD";
  case CMD_SOURCE_TCP:
    return "TCP";
  default:
    return "SYS";
  }
}

/**
 * @brief 验证航点位置合理性
 * @param wp 航点指针
 * @param source 命令来源
 * @return true = 合理, false = 不合理
 */
static bool ValidateWaypoint(const T_Waypoint *wp, E_CommandSource source) {
  (void)source;

  if (wp == NULL) {
    return false;
  }

  // 1. 经纬度范围检查
  if (wp->latitude < -90.0 || wp->latitude > 90.0) {
    USER_LOG_WARN(
        "Waypoint validation failed: latitude %.6f out of range [-90, 90]",
        wp->latitude);
    return false;
  }
  if (wp->longitude < -180.0 || wp->longitude > 180.0) {
    USER_LOG_WARN(
        "Waypoint validation failed: longitude %.6f out of range [-180, 180]",
        wp->longitude);
    return false;
  }

  // 2. 高度范围检查 (如果指定了高度)
  if (wp->altitude != 0) {
    if (wp->altitude < WAYPOINT_MIN_ALTITUDE_M ||
        wp->altitude > WAYPOINT_MAX_ALTITUDE_M) {
      USER_LOG_WARN(
          "Waypoint validation failed: altitude %.1f out of range [%.1f, %.1f]",
          wp->altitude, WAYPOINT_MIN_ALTITUDE_M, WAYPOINT_MAX_ALTITUDE_M);
      return false;
    }
  }

  // 3. 与当前位置距离检查，防止误下发过远航点
  T_DroneStatus status;
  if (DataSubscriber_GetDroneStatus(&status) !=
      DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_WARN("Waypoint validation failed: current global position unavailable");
    return false;
  }

  // Haversine公式计算距离
  double lat1 = status.latitude * M_PI / 180.0;
  double lon1 = status.longitude * M_PI / 180.0;
  double lat2 = wp->latitude * M_PI / 180.0;
  double lon2 = wp->longitude * M_PI / 180.0;

  double dlat = lat2 - lat1;
  double dlon = lon2 - lon1;

  double a = sin(dlat / 2) * sin(dlat / 2) +
             cos(lat1) * cos(lat2) * sin(dlon / 2) * sin(dlon / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  double distance = EARTH_RADIUS_M * c;

  if (distance > WAYPOINT_MAX_DISTANCE_M) {
    USER_LOG_WARN(
        "Waypoint validation failed: distance %.1fm exceeds max %.1fm",
        distance, WAYPOINT_MAX_DISTANCE_M);
    return false;
  }

  return true;
}

/************************ (C) COPYRIGHT DJI Innovations *****END OF FILE****/
