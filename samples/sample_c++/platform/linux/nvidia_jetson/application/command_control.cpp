/**
 * ********************************************************************
 * @file    command_control.cpp
 * @brief   飞行控制封装模块实现文件
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 * *********************************************************************
 */

#include "command_control.hpp"
#include "data.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <dji_flight_controller.h>
#include <dji_logger.h>
#include <dji_platform.h>
#include <mutex>
#include <thread>

/* Constants -----------------------------------------------------------------*/
#define DEG_TO_RAD 0.017453292519943295769
#define RAD_TO_DEG 57.295779513082320876
#define EARTH_RADIUS 6378137.0
#define ARRIVAL_THRESHOLD_XY_M 1.5    // Horizontal arrival threshold
#define ARRIVAL_THRESHOLD_Z_M 0.3     // Vertical arrival threshold
#define YAW_ARRIVAL_THRESHOLD_DEG 5.0 // Yaw arrival threshold
#define NAV_LOOP_INTERVAL_MS 50       // 20Hz Loop

/* Static Variables ----------------------------------------------------------*/
static std::mutex s_cmdMutex;
static std::atomic<bool> s_stopThread(false);
static std::thread s_navThread;
static std::deque<T_Waypoint> s_waypointQueue;
static T_Waypoint s_currentWaypoint;
static bool s_hasActiveWaypoint = false;
static E_NavigationStatus s_navStatus = NAV_STATUS_IDLE;
static T_DjiReturnCode s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
static T_DjiOsalHandler *s_osalHandler = nullptr;

/* Private Functions Declaration ---------------------------------------------*/
static void NavigationThreadFunc();
static void SleepMs(uint32_t sleepMs);

/* Exported Functions Definition ---------------------------------------------*/

T_DjiReturnCode CommandControl_Init(const T_CommandControlConfig *config) {
  if (config == nullptr) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  s_osalHandler = DjiPlatform_GetOsalHandler();

  T_DjiFlightControllerRidInfo ridInfo;
  ridInfo.latitude = config->ridLatitude * DEG_TO_RAD;
  ridInfo.longitude = config->ridLongitude * DEG_TO_RAD;
  ridInfo.altitude = config->ridAltitude;

  T_DjiReturnCode returnCode = DjiFlightController_Init(ridInfo);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("Flight controller init failed, error code: 0x%08llX",
                   returnCode);
    return returnCode;
  }

  // 设置自动返航高度为 100m
  returnCode = DjiFlightController_SetGoHomeAltitude(100);
  if (returnCode == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    E_DjiFlightControllerGoHomeAltitude getAltitude = 0;
    if (DjiFlightController_GetGoHomeAltitude(&getAltitude) == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
      USER_LOG_INFO("成功设置返航高度为: %d 米", getAltitude);
    }
  } else {
    USER_LOG_WARN("设置返航高度失败, 错误码: 0x%08llX", returnCode);
  }

  // Start navigation thread
  {
    std::lock_guard<std::mutex> lock(s_cmdMutex);
    s_stopThread = false;
    s_waypointQueue.clear();
    memset(&s_currentWaypoint, 0, sizeof(s_currentWaypoint));
    s_hasActiveWaypoint = false;
    s_navStatus = NAV_STATUS_IDLE;
    s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
  }
  s_navThread = std::thread(NavigationThreadFunc);

  USER_LOG_INFO("Command Control Initialized.");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode CommandControl_DeInit(void) {
  s_stopThread = true;
  if (s_navThread.joinable()) {
    s_navThread.join();
  }
  return DjiFlightController_DeInit();
}

T_DjiReturnCode CommandControl_ObtainJoystickAuthority(void) {
  return DjiFlightController_ObtainJoystickCtrlAuthority();
}

T_DjiReturnCode CommandControl_ReleaseJoystickAuthority(void) {
  return DjiFlightController_ReleaseJoystickCtrlAuthority();
}

T_DjiReturnCode CommandControl_Takeoff(void) {
  return DjiFlightController_StartTakeoff();
}

T_DjiReturnCode CommandControl_Land(void) {
  CommandControl_ClearNavigation();
  return DjiFlightController_StartLanding();
}

T_DjiReturnCode CommandControl_ConfirmLanding(void) {
  return DjiFlightController_StartConfirmLanding();
}

T_DjiReturnCode CommandControl_ForceLanding(void) {
  CommandControl_ClearNavigation();
  return DjiFlightController_StartForceLanding();
}

T_DjiReturnCode CommandControl_CancelLanding(void) {
  return DjiFlightController_CancelLanding();
}

T_DjiReturnCode CommandControl_GoHome(void) {
  CommandControl_ClearNavigation();
  return DjiFlightController_StartGoHome();
}

T_DjiReturnCode CommandControl_CancelGoHome(void) {
  return DjiFlightController_CancelGoHome();
}

T_DjiReturnCode
CommandControl_ExecuteVelocityControl(const T_VelocityCommand *cmd) {
  if (cmd == nullptr) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  T_DjiFlightControllerJoystickMode joystickMode = {
      DJI_FLIGHT_CONTROLLER_HORIZONTAL_VELOCITY_CONTROL_MODE,
      DJI_FLIGHT_CONTROLLER_VERTICAL_VELOCITY_CONTROL_MODE,
      DJI_FLIGHT_CONTROLLER_YAW_ANGLE_RATE_CONTROL_MODE,
      DJI_FLIGHT_CONTROLLER_HORIZONTAL_GROUND_COORDINATE,
      DJI_FLIGHT_CONTROLLER_STABLE_CONTROL_MODE_ENABLE,
  };
  DjiFlightController_SetJoystickMode(joystickMode);

  T_DjiFlightControllerJoystickCommand joystickCmd;
  joystickCmd.x = cmd->vx;
  joystickCmd.y = cmd->vy;
  joystickCmd.z = cmd->vz;
  joystickCmd.yaw = cmd->yawRate;

  T_DjiReturnCode returnCode =
      DjiFlightController_ExecuteJoystickAction(joystickCmd);
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_ERROR("执行Joystick动作失败, 错误码: 0x%08llX", returnCode);
  }
  return returnCode;
}

T_DjiReturnCode CommandControl_Hover(void) {
  T_VelocityCommand cmd = {0};
  return CommandControl_ExecuteVelocityControl(&cmd);
}

T_DjiReturnCode CommandControl_PlansTo(const T_Waypoint *waypoint) {
  if (waypoint == nullptr)
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;

  std::lock_guard<std::mutex> lock(s_cmdMutex);
  s_waypointQueue.push_back(*waypoint);
  s_navStatus = NAV_STATUS_RUNNING;
  s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;

  USER_LOG_INFO("Waypoint added to queue. Total: %d", s_waypointQueue.size());
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode CommandControl_ChangeTo(const T_Waypoint *waypoint) {
  if (waypoint == nullptr)
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;

  std::lock_guard<std::mutex> lock(s_cmdMutex);
  s_waypointQueue.clear();
  memset(&s_currentWaypoint, 0, sizeof(s_currentWaypoint));
  s_waypointQueue.push_back(*waypoint);
  s_hasActiveWaypoint = false;
  s_navStatus = NAV_STATUS_RUNNING;
  s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;

  USER_LOG_INFO("Navigation changed to new target.");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode CommandControl_StartNavigation(void) {
  std::lock_guard<std::mutex> lock(s_cmdMutex);

  if (s_navStatus == NAV_STATUS_RUNNING) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
  }

  if (!s_hasActiveWaypoint && s_waypointQueue.empty()) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT_IN_CURRENT_STATE;
  }

  s_navStatus = NAV_STATUS_RUNNING;
  s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
  USER_LOG_INFO("Navigation started.");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode CommandControl_StopNavigation(void) {
  {
    std::lock_guard<std::mutex> lock(s_cmdMutex);
    if (s_navStatus != NAV_STATUS_RUNNING) {
      return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }
    s_navStatus = NAV_STATUS_STOPPED;
  }

  CommandControl_Hover();
  USER_LOG_INFO("Navigation paused.");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode CommandControl_ResumeNavigation(void) {
  std::lock_guard<std::mutex> lock(s_cmdMutex);

  if (s_navStatus == NAV_STATUS_RUNNING) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
  }

  if (s_navStatus != NAV_STATUS_STOPPED) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT_IN_CURRENT_STATE;
  }

  if (!s_hasActiveWaypoint && s_waypointQueue.empty()) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_NONSUPPORT_IN_CURRENT_STATE;
  }

  s_navStatus = NAV_STATUS_RUNNING;
  s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
  USER_LOG_INFO("Navigation resumed.");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode CommandControl_ClearNavigation(void) {
  {
    std::lock_guard<std::mutex> lock(s_cmdMutex);
    s_waypointQueue.clear();
    memset(&s_currentWaypoint, 0, sizeof(s_currentWaypoint));
    s_hasActiveWaypoint = false;
    s_navStatus = NAV_STATUS_IDLE;
    s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
  }

  CommandControl_Hover();
  USER_LOG_INFO("Navigation cleared.");
  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode CommandControl_GetNavState(T_NavState *state) {
  if (state == nullptr) {
    return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
  }

  std::lock_guard<std::mutex> lock(s_cmdMutex);

  state->status = s_navStatus;
  state->isNavigating = (s_navStatus == NAV_STATUS_RUNNING);
  state->hasActiveTarget = s_hasActiveWaypoint;
  state->lastErrorCode = s_lastNavError;

  if (s_hasActiveWaypoint) {
    state->currentTarget = s_currentWaypoint;
  } else {
    memset(&state->currentTarget, 0, sizeof(T_Waypoint));
  }

  // Copy queue (up to NAV_MAX_QUEUE_REPORT items)
  state->queueCount = (s_waypointQueue.size() > NAV_MAX_QUEUE_REPORT)
                          ? NAV_MAX_QUEUE_REPORT
                          : (uint8_t)s_waypointQueue.size();

  for (uint8_t i = 0; i < state->queueCount; i++) {
    state->queue[i] = s_waypointQueue[i];
  }

  return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

const char *
CommandControl_GetNavigationStatusString(E_NavigationStatus status) {
  switch (status) {
  case NAV_STATUS_IDLE:
    return "IDLE";
  case NAV_STATUS_RUNNING:
    return "RUNNING";
  case NAV_STATUS_STOPPED:
    return "STOPPED";
  case NAV_STATUS_COMPLETED:
    return "COMPLETED";
  case NAV_STATUS_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

/* Private Functions Definition ----------------------------------------------*/

static void NavigationThreadFunc() {
  uint32_t loopCounter = 0;

  while (!s_stopThread) {
    E_NavigationStatus navStatus;
    {
      std::lock_guard<std::mutex> lock(s_cmdMutex);
      navStatus = s_navStatus;
    }

    loopCounter++;
    if (navStatus != NAV_STATUS_RUNNING) {
      SleepMs(100);
      continue;
    }

    T_Waypoint target;
    bool hasTarget = false;

    {
      std::lock_guard<std::mutex> lock(s_cmdMutex);
      if (s_hasActiveWaypoint) {
        target = s_currentWaypoint;
        hasTarget = true;
      } else if (!s_waypointQueue.empty()) {
        s_currentWaypoint = s_waypointQueue.front();
        s_waypointQueue.pop_front();
        target = s_currentWaypoint;
        s_hasActiveWaypoint = true;
        hasTarget = true;
        s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
        USER_LOG_INFO("开始飞往目标点 - 纬度: %f, 经度: %f, 高度: %f",
                      target.latitude, target.longitude, target.altitude);
      } else {
        s_navStatus = NAV_STATUS_COMPLETED;
        USER_LOG_INFO("所有航点已完成");
      }
    }

    if (!hasTarget) {
      CommandControl_Hover();
      SleepMs(100);
      continue;
    }

    // Get Drone Status
    T_DroneStatus droneStatus;
    if (DataSubscriber_GetDroneStatus(&droneStatus) !=
        DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
      {
        std::lock_guard<std::mutex> lock(s_cmdMutex);
        s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
      }
      USER_LOG_WARN("导航: 获取无人机状态失败");
      SleepMs(NAV_LOOP_INTERVAL_MS);
      continue;
    }

    double curLat = droneStatus.latitude;
    double curLon = droneStatus.longitude;
    // Check for invalid GPS
    if (std::abs(curLat) < 0.00001 && std::abs(curLon) < 0.00001) {
      {
        std::lock_guard<std::mutex> lock(s_cmdMutex);
        s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
      }
      USER_LOG_WARN("导航: 无效GPS信号 (0,0)");
      SleepMs(NAV_LOOP_INTERVAL_MS);
      continue;
    }

    double curZ = droneStatus.altitude;
    double curYaw = droneStatus.yaw;

    // Calculate NED Offsets
    double latRad = curLat * DEG_TO_RAD;
    double dLat = (target.latitude - curLat) * DEG_TO_RAD;
    double dLon = (target.longitude - curLon) * DEG_TO_RAD;

    double diffN = dLat * EARTH_RADIUS;
    double diffE = dLon * EARTH_RADIUS * cos(latRad);
    double diffD = target.altitude - curZ; // Target Alt - Current Alt (Up is +)

    double distHorizSq = diffN * diffN + diffE * diffE;
    double distSq = distHorizSq + diffD * diffD;
    double distTotal = sqrt(distSq);

    // Yaw Error
    double yawErr = target.heading - curYaw;
    while (yawErr > 180.0)
      yawErr -= 360.0;
    while (yawErr < -180.0)
      yawErr += 360.0;

    // Output debug info every 2 seconds (assuming 50ms loop = 20Hz, so 40
    // counts)
    if (loopCounter % 40 == 0) {
      USER_LOG_INFO("导航中... 剩余距离: %.2f 米, 当前高度: %.2f 米", distTotal,
                    curZ);
      USER_LOG_INFO("状态: %d (飞行状态), %d (显示模式)",
                    droneStatus.flightStatus, droneStatus.displayMode);

      // Diagnosing the 450m limit issue:
      // If we are far away, log special warning ?
    }

    bool posHorizReached =
        distHorizSq < (ARRIVAL_THRESHOLD_XY_M * ARRIVAL_THRESHOLD_XY_M);
    bool posVertReached = std::abs(diffD) < ARRIVAL_THRESHOLD_Z_M;
    bool yawReached = std::abs(yawErr) < YAW_ARRIVAL_THRESHOLD_DEG;

    if (posHorizReached && posVertReached && yawReached) {
      USER_LOG_INFO("已到达航点");

      // Hover and Wait
      CommandControl_Hover();
      if (target.stayTime > 0) {
        USER_LOG_INFO("悬停等待 %d 秒...", target.stayTime);
        if (s_osalHandler)
          s_osalHandler->TaskSleepMs(target.stayTime * 1000);
        else
          std::this_thread::sleep_for(std::chrono::seconds(target.stayTime));
      }

      std::lock_guard<std::mutex> lock(s_cmdMutex);
      s_hasActiveWaypoint = false;
      memset(&s_currentWaypoint, 0, sizeof(s_currentWaypoint));
      s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
      if (s_waypointQueue.empty()) {
        s_navStatus = NAV_STATUS_COMPLETED;
      }
    } else {
      // Control Loop
      // P-Controller
      double kpPos = 0.8;
      double kpVert = 1.0;
      double kpYaw = 1.5;

      double vN = diffN * kpPos;
      double vE = diffE * kpPos;
      double vD = diffD * kpVert;
      double vYaw = yawErr * kpYaw;

      // Speed Limits
      double vHorizMag = sqrt(vN * vN + vE * vE);
      double maxSpeed =
          target.speed > 0 ? target.speed : 5.0; // Default 5m/s if 0

      if (vHorizMag > maxSpeed) {
        double scale = maxSpeed / vHorizMag;
        vN *= scale;
        vE *= scale;
      }

      // Vertical Speed Limit
      if (vD > 3.0)
        vD = 3.0;
      if (vD < -3.0)
        vD = -3.0;

      // Yaw Rate Limit
      if (vYaw > 60.0)
        vYaw = 60.0;
      if (vYaw < -60.0)
        vYaw = -60.0;

      T_VelocityCommand cmd;
      cmd.vx = (float)vN;
      cmd.vy = (float)vE;
      cmd.vz = (float)vD;
      cmd.yawRate = (float)vYaw;

      {
        std::lock_guard<std::mutex> lock(s_cmdMutex);
        if (s_navStatus != NAV_STATUS_RUNNING) {
          USER_LOG_INFO("导航已停止，跳过速度指令发送");
          continue;
        }
      }

      T_DjiReturnCode execRet = CommandControl_ExecuteVelocityControl(&cmd);
      if (execRet != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        {
          std::lock_guard<std::mutex> lock(s_cmdMutex);
          s_lastNavError = execRet;
        }
        USER_LOG_ERROR("发送速度指令失败: 0x%08llX. 尝试重新获取控制权...",
                       execRet);

        DjiFlightController_ObtainJoystickCtrlAuthority();
      } else {
        std::lock_guard<std::mutex> lock(s_cmdMutex);
        s_lastNavError = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
      }

      SleepMs(NAV_LOOP_INTERVAL_MS);
    }
  }
}

static void SleepMs(uint32_t sleepMs) {
  if (s_osalHandler) {
    s_osalHandler->TaskSleepMs(sleepMs);
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
  }
}
