/**
 ********************************************************************
 * @file    dji_sdk_app_info.h
 * @brief   This is the header file for defining the structure and (exported) function prototypes.
 *
 * @copyright (c) 2018 DJI. All rights reserved.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef DJI_SDK_APP_INFO_H
#define DJI_SDK_APP_INFO_H

/* Includes ------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif

/* Exported constants --------------------------------------------------------*/
// Never commit real DJI application credentials. Copy
// dji_sdk_app_info.local.h.example to dji_sdk_app_info.local.h and fill it
// locally. The local file is ignored by Git.
#if defined(__has_include)
#if __has_include("dji_sdk_app_info.local.h")
#include "dji_sdk_app_info.local.h"
#define DJI_SDK_LOCAL_APP_INFO_INCLUDED 1
#endif
#endif

#ifndef DJI_SDK_LOCAL_APP_INFO_INCLUDED
#define USER_APP_NAME               "REPLACE_ME"
#define USER_APP_ID                 "REPLACE_ME"
#define USER_APP_KEY                "REPLACE_ME"
#define USER_APP_LICENSE            "REPLACE_ME"
#define USER_DEVELOPER_ACCOUNT      "REPLACE_ME"
#define USER_BAUD_RATE              "460800"
#endif

/* Exported types ------------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/


#ifdef __cplusplus
}
#endif

#endif // DJI_SDK_APP_INFO_H
/************************ (C) COPYRIGHT DJI Innovations *******END OF FILE******/
