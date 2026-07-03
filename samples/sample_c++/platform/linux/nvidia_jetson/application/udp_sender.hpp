/**
 ********************************************************************
 * @file    udp_sender.hpp
 * @brief   UDP数据发送模块头文件
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 *********************************************************************
 */

#ifndef UDP_SENDER_HPP
#define UDP_SENDER_HPP

/* Includes ------------------------------------------------------------------*/
#include "data.hpp"
#include <dji_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported constants --------------------------------------------------------*/
#define UDP_DEFAULT_PORT 14550
#define UDP_DEFAULT_IP "192.168.0.1" // 默认目标IP (主机)
#define UDP_IMPORTANT_SEND_INTERVAL_MS 100
#define UDP_FULL_SEND_INTERVAL_MS 5000
#define UDP_SEND_INTERVAL_MS UDP_IMPORTANT_SEND_INTERVAL_MS

/* Exported types ------------------------------------------------------------*/

/**
 * @brief UDP发送器配置
 */
typedef struct {
  char targetIp[64];            // 目标IP地址
  uint16_t targetPort;          // 目标端口
  uint32_t importantIntervalMs; // GPS/RTK重要包发送间隔 (毫秒)
  uint32_t fullIntervalMs;      // 全量包发送间隔 (毫秒)
} T_UdpSenderConfig;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化UDP发送模块
 * @param targetIp 目标IP地址
 * @param targetPort 目标端口
 * @return 执行结果
 */
T_DjiReturnCode UdpSender_Init(const char *targetIp, uint16_t targetPort);

/**
 * @brief 使用自定义配置初始化UDP发送模块
 * @param config UDP发送器配置
 * @return 执行结果
 */
T_DjiReturnCode UdpSender_InitWithConfig(const T_UdpSenderConfig *config);

/**
 * @brief 设置UDP分频发送速率
 * @param importantIntervalMs GPS/RTK重要包发送间隔 (毫秒)
 * @param fullIntervalMs 全量包发送间隔 (毫秒)
 * @return 执行结果
 */
T_DjiReturnCode UdpSender_SetSendRates(uint32_t importantIntervalMs,
                                       uint32_t fullIntervalMs);

/**
 * @brief 反初始化UDP发送模块
 * @return 执行结果
 */
T_DjiReturnCode UdpSender_DeInit(void);

/**
 * @brief 发送无人机状态数据
 * @param status 无人机状态数据
 * @return 执行结果
 */
T_DjiReturnCode UdpSender_SendDroneStatus(const T_DroneStatus *status);

/**
 * @brief 获取并发送最新的无人机状态
 * @return 执行结果
 */
T_DjiReturnCode UdpSender_SendLatestStatus(void);

/**
 * @brief 检查UDP发送器是否已初始化
 * @return true: 已初始化, false: 未初始化
 */
bool UdpSender_IsInitialized(void);

/**
 * @brief 使用默认配置初始化UDP发送器
 * @return 执行结果
 * @note 使用 UDP_DEFAULT_IP 和 UDP_DEFAULT_PORT 初始化
 */
T_DjiReturnCode UdpSender_InitDefault(void);

/**
 * @brief 启动状态发送线程
 * @return 执行结果
 * @note 线程以 UDP_SEND_INTERVAL_MS 间隔循环发送无人机状态
 */
T_DjiReturnCode UdpSender_StartStatusThread(void);

/**
 * @brief 停止状态发送线程
 * @return 执行结果
 */
T_DjiReturnCode UdpSender_StopStatusThread(void);

#ifdef __cplusplus
}
#endif

#endif // UDP_SENDER_HPP
