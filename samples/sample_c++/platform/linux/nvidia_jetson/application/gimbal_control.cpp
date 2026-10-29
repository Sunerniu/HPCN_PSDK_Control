/**
 * ********************************************************************
 * @file    gimbal_control.cpp
 * @brief   云台控制模块实现 (Gimbal Control Implementation)
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 * *********************************************************************
 */

#include "gimbal_control.hpp"
#include <dji_logger.h>
#include <dji_platform.h>

/* Constants -----------------------------------------------------------------*/
static const E_DjiMountPosition s_mountPosition = DJI_MOUNT_POSITION_PAYLOAD_PORT_NO2;

/* Exported Functions Definition ---------------------------------------------*/

T_DjiReturnCode GimbalControl_Init(void) {
    T_DjiReturnCode returnCode;

    USER_LOG_INFO("Init Gimbal Manager Module...");
    returnCode = DjiGimbalManager_Init();
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Init gimbal manager failed, error code: 0x%08X", returnCode);
        return returnCode;
    }

    // Set gimbal mode to Free Mode as required by the docs
    returnCode = DjiGimbalManager_SetMode(s_mountPosition, DJI_GIMBAL_MODE_FREE);
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Set gimbal mode failed, error code: 0x%08X", returnCode);
        return returnCode;
    }

    USER_LOG_INFO("Gimbal Manager Initialized.");
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode GimbalControl_DeInit(void) {
    return DjiGimbalManager_Deinit();
}

T_DjiReturnCode GimbalControl_Rotate(float pitch, float roll, float yaw, float time) {
    T_DjiReturnCode returnCode;
    T_DjiGimbalManagerRotation rotation;

    USER_LOG_INFO("Target gimbal pry = (%.1f, %.1f, %.1f) time=%.1f in the absolute coordinate system", pitch, roll, yaw, time);

    // Absolute Angle Control
    // Time unit in PSDK struct is 0.1s or 0.5 depending on specific config, use document format
    // rotation.time is defined in documentation, double check the struct typedef (usually wait time in seconds for the rotation)
    rotation.rotationMode = DJI_GIMBAL_ROTATION_MODE_ABSOLUTE_ANGLE;
    rotation.pitch = pitch;
    rotation.roll = roll;
    rotation.yaw = yaw;
    rotation.time = time > 0 ? time : 0.5;

    returnCode = DjiGimbalManager_Rotate(s_mountPosition, rotation);
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Gimbal rotate failed, error code: 0x%08X", returnCode);
    }
    return returnCode;
}

T_DjiReturnCode GimbalControl_Reset(void) {
    T_DjiReturnCode returnCode;
    
    USER_LOG_INFO("Target gimbal reset.\r\n");
    // Call reset with yaw and pitch reset mode
    returnCode = DjiGimbalManager_Reset(s_mountPosition, DJI_GIMBAL_RESET_MODE_PITCH_AND_YAW);
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Reset gimbal failed, error code: 0x%08X", returnCode);
    }
    return returnCode;
}

/************************ (C) COPYRIGHT DJI Innovations *****END OF FILE****/
