/**
 ********************************************************************
 * @file    hal_network.c
 * @version V2.0.0
 * @date    3/27/20
 * @brief
 *
 * @copyright (c) 2021 DJI. All rights reserved.
 *
 * All information contained herein is, and remains, the property of DJI.
 * The intellectual and technical concepts contained herein are proprietary
 * to DJI and may be covered by U.S. and foreign patents, patents in process,
 * and protected by trade secret or copyright law.  Dissemination of this
 * information, including but not limited to data and other proprietary
 * material(s) incorporated within the information, in any form, is strictly
 * prohibited without the express written consent of DJI.
 *
 * If you receive this source code without DJI’s authorization, you may not
 * further disseminate the information, and you must immediately remove the
 * source code and notify DJI of its removal. DJI reserves the right to pursue
 * legal actions against you for any loss(es) or damage(s) caused by your
 * failure to do so.
 *
 *********************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "hal_network.h"
#include "dji_logger.h"
#include <ctype.h>
#include <errno.h>

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* Private values -------------------------------------------------------------*/

/* Private functions declaration ---------------------------------------------*/
static const char *GetNetworkDeviceName(void);
static uint16_t GetUsbIdFromEnvironment(const char *name, uint16_t defaultValue);

/* Exported functions definition ---------------------------------------------*/
T_DjiReturnCode HalNetWork_Init(const char *ipAddr, const char *netMask, T_DjiNetworkHandle *halObj)
{
    int32_t ret;
    char cmdStr[LINUX_CMD_STR_MAX_SIZE];

    if (ipAddr == NULL || netMask == NULL) {
        USER_LOG_ERROR("hal network config param error");
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }

    //Attention: need root permission to config ip addr and netmask.
    memset(cmdStr, 0, sizeof(cmdStr));

    const char *networkDevice = GetNetworkDeviceName();
    snprintf(cmdStr, sizeof(cmdStr), "ifconfig %s up", networkDevice);
    ret = system(cmdStr);
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Can't open the network."
                       "Probably the program not execute with root permission."
                       "Please use the root permission to execute the program.");
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    snprintf(cmdStr, sizeof(cmdStr), "ifconfig %s %s netmask %s", networkDevice, ipAddr, netMask);
    ret = system(cmdStr);
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Can't config the ip address of network."
                       "Probably the program not execute with root permission."
                       "Please use the root permission to execute the program.");
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalNetWork_DeInit(T_DjiNetworkHandle halObj)
{
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalNetWork_GetDeviceInfo(T_DjiHalNetworkDeviceInfo *deviceInfo)
{
    deviceInfo->usbNetAdapter.vid = GetUsbIdFromEnvironment("DJI_NETWORK_USB_VID", USB_NET_ADAPTER_VID);
    deviceInfo->usbNetAdapter.pid = GetUsbIdFromEnvironment("DJI_NETWORK_USB_PID", USB_NET_ADAPTER_PID);

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

/* Private functions definition-----------------------------------------------*/
static const char *GetNetworkDeviceName(void)
{
    const char *device = getenv("DJI_NETWORK_DEVICE");
    if (device == NULL || device[0] == '\0') {
        return LINUX_NETWORK_DEV;
    }

    for (const char *cursor = device; *cursor != '\0'; ++cursor) {
        if (!isalnum((unsigned char) *cursor) && *cursor != '_' && *cursor != '-' && *cursor != '.' && *cursor != ':') {
            USER_LOG_WARN("Invalid DJI_NETWORK_DEVICE, using %s", LINUX_NETWORK_DEV);
            return LINUX_NETWORK_DEV;
        }
    }
    return device;
}

static uint16_t GetUsbIdFromEnvironment(const char *name, uint16_t defaultValue)
{
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return defaultValue;
    }

    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(value, &end, 0);
    if (errno != 0 || end == value || *end != '\0' || parsed > 0xFFFF) {
        USER_LOG_WARN("Invalid %s, using 0x%04X", name, defaultValue);
        return defaultValue;
    }
    return (uint16_t) parsed;
}

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
