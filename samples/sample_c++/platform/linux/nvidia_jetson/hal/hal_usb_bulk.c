/**
 ********************************************************************
 * @file    hal_usb_bulk.c
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
#include "hal_usb_bulk.h"
#include "dji_logger.h"
#include <errno.h>
#include <stdlib.h>

/* Private constants ---------------------------------------------------------*/
#define LINUX_USB_BULK_TRANSFER_TIMEOUT_MS    (50)
#define LINUX_USB_BULK_TRANSFER_WAIT_FOREVER  (-1)

/* Private types -------------------------------------------------------------*/
typedef struct {
#ifdef LIBUSB_INSTALLED
    libusb_device_handle *handle;
#else
    void *handle;
#endif
    int32_t ep1;
    int32_t ep2;
    uint32_t interfaceNum;
    T_DjiHalUsbBulkInfo usbBulkInfo;
} T_HalUsbBulkObj;

/* Private values -------------------------------------------------------------*/

/* Private functions declaration ---------------------------------------------*/
static uint16_t GetUsbValueFromEnvironment(const char *name, uint16_t defaultValue);
static const char *GetUsbPathFromEnvironment(const char *name, const char *defaultValue);

/* Exported functions definition ---------------------------------------------*/
T_DjiReturnCode HalUsbBulk_Init(T_DjiHalUsbBulkInfo usbBulkInfo, T_DjiUsbBulkHandle *usbBulkHandle)
{
    int32_t ret;
    struct libusb_device_handle *handle = NULL;

    *usbBulkHandle = malloc(sizeof(T_HalUsbBulkObj));
    if (*usbBulkHandle == NULL) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    if (usbBulkInfo.isUsbHost == true) {
#ifdef LIBUSB_INSTALLED
        ret = libusb_init(NULL);
        if (ret < 0) {
            return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }

        handle = libusb_open_device_with_vid_pid(NULL, usbBulkInfo.vid, usbBulkInfo.pid);
        if (handle == NULL) {
            return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }

        ret = libusb_claim_interface(handle, usbBulkInfo.channelInfo.interfaceNum);
        if (ret != LIBUSB_SUCCESS) {
            printf("libusb claim interface error");
            libusb_close(handle);
            return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }

        ((T_HalUsbBulkObj *) *usbBulkHandle)->handle = handle;
        memcpy(&((T_HalUsbBulkObj *) *usbBulkHandle)->usbBulkInfo, &usbBulkInfo, sizeof(usbBulkInfo));
#endif
    } else {
        ((T_HalUsbBulkObj *) *usbBulkHandle)->handle = handle;
        memcpy(&((T_HalUsbBulkObj *) *usbBulkHandle)->usbBulkInfo, &usbBulkInfo, sizeof(usbBulkInfo));
        ((T_HalUsbBulkObj *) *usbBulkHandle)->interfaceNum = usbBulkInfo.channelInfo.interfaceNum;

        const uint16_t bulk1Interface = GetUsbValueFromEnvironment("DJI_USB_BULK1_INTERFACE",
                                                                   LINUX_USB_BULK1_INTERFACE_NUM);
        const uint16_t bulk2Interface = GetUsbValueFromEnvironment("DJI_USB_BULK2_INTERFACE",
                                                                   LINUX_USB_BULK2_INTERFACE_NUM);
        if (usbBulkInfo.channelInfo.interfaceNum == bulk1Interface) {
            const char *epOut = GetUsbPathFromEnvironment("DJI_USB_BULK1_EP_OUT_PATH",
                                                          LINUX_USB_BULK1_EP_OUT_FD);
            const char *epIn = GetUsbPathFromEnvironment("DJI_USB_BULK1_EP_IN_PATH",
                                                         LINUX_USB_BULK1_EP_IN_FD);
            ((T_HalUsbBulkObj *) *usbBulkHandle)->ep1 = open(epOut, O_RDWR);
            if (((T_HalUsbBulkObj *) *usbBulkHandle)->ep1 < 0) {
                USER_LOG_ERROR("Open USB Bulk1 OUT endpoint failed (%s): %s", epOut, strerror(errno));
                free(*usbBulkHandle);
                *usbBulkHandle = NULL;
                return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
            }

            ((T_HalUsbBulkObj *) *usbBulkHandle)->ep2 = open(epIn, O_RDWR);
            if (((T_HalUsbBulkObj *) *usbBulkHandle)->ep2 < 0) {
                USER_LOG_ERROR("Open USB Bulk1 IN endpoint failed (%s): %s", epIn, strerror(errno));
                close(((T_HalUsbBulkObj *) *usbBulkHandle)->ep1);
                free(*usbBulkHandle);
                *usbBulkHandle = NULL;
                return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
            }
        } else if (usbBulkInfo.channelInfo.interfaceNum == bulk2Interface) {
            const char *epOut = GetUsbPathFromEnvironment("DJI_USB_BULK2_EP_OUT_PATH",
                                                          LINUX_USB_BULK2_EP_OUT_FD);
            const char *epIn = GetUsbPathFromEnvironment("DJI_USB_BULK2_EP_IN_PATH",
                                                         LINUX_USB_BULK2_EP_IN_FD);
            ((T_HalUsbBulkObj *) *usbBulkHandle)->ep1 = open(epOut, O_RDWR);
            if (((T_HalUsbBulkObj *) *usbBulkHandle)->ep1 < 0) {
                USER_LOG_ERROR("Open USB Bulk2 OUT endpoint failed (%s): %s", epOut, strerror(errno));
                free(*usbBulkHandle);
                *usbBulkHandle = NULL;
                return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
            }

            ((T_HalUsbBulkObj *) *usbBulkHandle)->ep2 = open(epIn, O_RDWR);
            if (((T_HalUsbBulkObj *) *usbBulkHandle)->ep2 < 0) {
                USER_LOG_ERROR("Open USB Bulk2 IN endpoint failed (%s): %s", epIn, strerror(errno));
                close(((T_HalUsbBulkObj *) *usbBulkHandle)->ep1);
                free(*usbBulkHandle);
                *usbBulkHandle = NULL;
                return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
            }
        } else {
            USER_LOG_ERROR("Unknown USB Bulk interface: %u", usbBulkInfo.channelInfo.interfaceNum);
            free(*usbBulkHandle);
            *usbBulkHandle = NULL;
            return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
        }
    }

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalUsbBulk_DeInit(T_DjiUsbBulkHandle usbBulkHandle)
{
    struct libusb_device_handle *handle = NULL;
    T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();

    if (usbBulkHandle == NULL) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    handle = ((T_HalUsbBulkObj *) usbBulkHandle)->handle;

    if (((T_HalUsbBulkObj *) usbBulkHandle)->usbBulkInfo.isUsbHost == true) {
#ifdef LIBUSB_INSTALLED
        libusb_release_interface(handle, ((T_HalUsbBulkObj *) usbBulkHandle)->usbBulkInfo.channelInfo.interfaceNum);
        osalHandler->TaskSleepMs(100);
        libusb_exit(NULL);
#endif
    } else {
        close(((T_HalUsbBulkObj *) usbBulkHandle)->ep1);
        close(((T_HalUsbBulkObj *) usbBulkHandle)->ep2);
    }

    free(usbBulkHandle);

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalUsbBulk_WriteData(T_DjiUsbBulkHandle usbBulkHandle, const uint8_t *buf, uint32_t len,
                                     uint32_t *realLen)
{
    int32_t ret;
    int32_t actualLen;
    struct libusb_device_handle *handle = NULL;

    if (usbBulkHandle == NULL) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    handle = ((T_HalUsbBulkObj *) usbBulkHandle)->handle;

    if (((T_HalUsbBulkObj *) usbBulkHandle)->usbBulkInfo.isUsbHost == true) {
#ifdef LIBUSB_INSTALLED
        ret = libusb_bulk_transfer(handle, ((T_HalUsbBulkObj *) usbBulkHandle)->usbBulkInfo.channelInfo.endPointOut,
                                   (uint8_t *) buf, len, &actualLen, LINUX_USB_BULK_TRANSFER_TIMEOUT_MS);
        if (ret < 0) {
            USER_LOG_ERROR("Write usb bulk data failed, errno = %d", ret);
            return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }

        *realLen = actualLen;
#endif
    } else {
        const ssize_t written = write(((T_HalUsbBulkObj *) usbBulkHandle)->ep1, buf, len);
        if (written < 0) {
            USER_LOG_ERROR("Write USB Bulk endpoint failed: %s", strerror(errno));
            return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }
        *realLen = (uint32_t) written;
    }

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalUsbBulk_ReadData(T_DjiUsbBulkHandle usbBulkHandle, uint8_t *buf, uint32_t len,
                                    uint32_t *realLen)
{
    int32_t ret;
    struct libusb_device_handle *handle = NULL;
    int32_t actualLen;

    if (usbBulkHandle == NULL) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    handle = ((T_HalUsbBulkObj *) usbBulkHandle)->handle;

    if (((T_HalUsbBulkObj *) usbBulkHandle)->usbBulkInfo.isUsbHost == true) {
#ifdef LIBUSB_INSTALLED
        ret = libusb_bulk_transfer(handle, ((T_HalUsbBulkObj *) usbBulkHandle)->usbBulkInfo.channelInfo.endPointIn,
                                   buf, len, &actualLen, LINUX_USB_BULK_TRANSFER_WAIT_FOREVER);
        if (ret < 0) {
            USER_LOG_ERROR("Read usb bulk data failed, errno = %d", ret);
            return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }

        *realLen = actualLen;
#endif
    } else {
        const ssize_t readLen = read(((T_HalUsbBulkObj *) usbBulkHandle)->ep2, buf, len);
        if (readLen < 0) {
            USER_LOG_ERROR("Read USB Bulk endpoint failed: %s", strerror(errno));
            return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }
        *realLen = (uint32_t) readLen;
    }

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode HalUsbBulk_GetDeviceInfo(T_DjiHalUsbBulkDeviceInfo *deviceInfo)
{
    //attention: this interface only be called in usb device mode.
    deviceInfo->vid = GetUsbValueFromEnvironment("DJI_USB_BULK_VID", LINUX_USB_VID);
    deviceInfo->pid = GetUsbValueFromEnvironment("DJI_USB_BULK_PID", LINUX_USB_PID);

    // This bulk channel is used to obtain DJI camera video stream and push 3rd-party camera video stream.
    deviceInfo->channelInfo[DJI_HAL_USB_BULK_NUM_0].interfaceNum =
        GetUsbValueFromEnvironment("DJI_USB_BULK1_INTERFACE", LINUX_USB_BULK1_INTERFACE_NUM);
    deviceInfo->channelInfo[DJI_HAL_USB_BULK_NUM_0].endPointIn =
        GetUsbValueFromEnvironment("DJI_USB_BULK1_EP_IN", LINUX_USB_BULK1_END_POINT_IN);
    deviceInfo->channelInfo[DJI_HAL_USB_BULK_NUM_0].endPointOut =
        GetUsbValueFromEnvironment("DJI_USB_BULK1_EP_OUT", LINUX_USB_BULK1_END_POINT_OUT);

    // This bulk channel is used to obtain DJI perception image and download camera media file.
    deviceInfo->channelInfo[DJI_HAL_USB_BULK_NUM_1].interfaceNum =
        GetUsbValueFromEnvironment("DJI_USB_BULK2_INTERFACE", LINUX_USB_BULK2_INTERFACE_NUM);
    deviceInfo->channelInfo[DJI_HAL_USB_BULK_NUM_1].endPointIn =
        GetUsbValueFromEnvironment("DJI_USB_BULK2_EP_IN", LINUX_USB_BULK2_END_POINT_IN);
    deviceInfo->channelInfo[DJI_HAL_USB_BULK_NUM_1].endPointOut =
        GetUsbValueFromEnvironment("DJI_USB_BULK2_EP_OUT", LINUX_USB_BULK2_END_POINT_OUT);

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

/* Private functions definition-----------------------------------------------*/
static uint16_t GetUsbValueFromEnvironment(const char *name, uint16_t defaultValue)
{
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return defaultValue;
    }

    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(value, &end, 0);
    if (errno != 0 || end == value || *end != '\0' || parsed > 0xFFFF) {
        USER_LOG_WARN("Invalid %s, using 0x%X", name, defaultValue);
        return defaultValue;
    }
    return (uint16_t) parsed;
}

static const char *GetUsbPathFromEnvironment(const char *name, const char *defaultValue)
{
    const char *value = getenv(name);
    return value == NULL || value[0] == '\0' ? defaultValue : value;
}

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
