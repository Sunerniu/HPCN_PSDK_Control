/**
 ********************************************************************
 * @file    udp_receiver.hpp
 * @brief   UDP命令接收模块头文件 - 接收PC发送的控制命令
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 *********************************************************************
 */

#ifndef UDP_RECEIVER_HPP
#define UDP_RECEIVER_HPP

/* Includes ----------------------------------------------------------*/
#include "protocol.h"
#include <dji_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported constants ------------------------------------------------*/
#define UDP_RECEIVER_DEFAULT_PORT 14551 // 命令接收端口 (与发送端口不同)

/* Exported types ----------------------------------------------------*/

/**
 * @brief 命令处理回调函数类型
 * @param packet 收到的控制包
 * @return 处理结果 (0=成功, 其他=错误)
 */
typedef int (*UdpCommandHandler)(const T_ControlPacket *packet);

/* Exported functions ------------------------------------------------*/

/**
 * @brief 初始化UDP接收模块
 * @param port 监听端口
 * @return 执行结果
 */
T_DjiReturnCode UdpReceiver_Init(uint16_t port);

/**
 * @brief 使用默认端口初始化
 * @return 执行结果
 */
T_DjiReturnCode UdpReceiver_InitDefault(void);

/**
 * @brief 反初始化UDP接收模块
 * @return 执行结果
 */
T_DjiReturnCode UdpReceiver_DeInit(void);

/**
 * @brief 启动接收线程
 * @return 执行结果
 */
T_DjiReturnCode UdpReceiver_Start(void);

/**
 * @brief 停止接收线程
 * @return 执行结果
 */
T_DjiReturnCode UdpReceiver_Stop(void);

/**
 * @brief 注册命令处理回调
 * @param handler 回调函数
 * @return 执行结果
 */
T_DjiReturnCode UdpReceiver_RegisterCommandHandler(UdpCommandHandler handler);

/**
 * @brief 检查是否正在运行
 * @return true = 运行中
 */
bool UdpReceiver_IsRunning(void);

#ifdef __cplusplus
}
#endif

#endif // UDP_RECEIVER_HPP
