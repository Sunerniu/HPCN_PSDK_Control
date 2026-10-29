/**
 ********************************************************************
 * @file    tcp_server.hpp
 * @brief   TCP服务器模块头文件 - AGX端接收PC控制命令
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 *********************************************************************
 */

#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

/* Includes ------------------------------------------------------------------*/
#include "protocol.h"
#include <dji_typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported constants --------------------------------------------------------*/
#define TCP_SERVER_DEFAULT_PORT 8080    // 默认监听端口
#define TCP_SERVER_MAX_CLIENTS 1        // 最大客户端数量 (目前只支持1个)
#define TCP_SERVER_RECV_TIMEOUT_MS 1000 // 接收超时 (毫秒)
#define TCP_SERVER_DISCONNECT_TIMEOUT_MS                                       \
  10000 // 断线超时阈值 (10秒，超时后自动停止导航)

/* Exported types ------------------------------------------------------------*/

/**
 * @brief TCP服务器状态
 */
typedef enum {
  TCP_SERVER_STATE_IDLE = 0,  // 空闲
  TCP_SERVER_STATE_LISTENING, // 监听中
  TCP_SERVER_STATE_CONNECTED, // 已连接客户端
  TCP_SERVER_STATE_ERROR,     // 错误
} E_TcpServerState;

/**
 * @brief 命令处理回调函数类型
 * @param packet 收到的控制包
 * @return 处理结果 (0=成功, 其他=错误)
 */
typedef int (*TcpCommandHandler)(const T_ControlPacket *packet);

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 初始化TCP服务器
 * @param port 监听端口 (推荐8080)
 * @return 执行结果
 */
T_DjiReturnCode TcpServer_Init(uint16_t port);

/**
 * @brief 反初始化TCP服务器
 * @return 执行结果
 */
T_DjiReturnCode TcpServer_DeInit(void);

/**
 * @brief 启动TCP服务器 (开始监听)
 * @return 执行结果
 * @note 此函数创建一个监听线程，立即返回
 */
T_DjiReturnCode TcpServer_Start(void);

/**
 * @brief 初始化并启动TCP服务器
 * @return 执行结果
 * @note 使用默认端口 TCP_SERVER_DEFAULT_PORT 初始化并启动
 */
T_DjiReturnCode TcpServer_InitAndStart(void);

/**
 * @brief 停止TCP服务器
 * @return 执行结果
 */
T_DjiReturnCode TcpServer_Stop(void);

/**
 * @brief 获取服务器状态
 * @return 服务器状态
 */
E_TcpServerState TcpServer_GetState(void);

/**
 * @brief 检查是否有客户端连接
 * @return true = 已连接, false = 未连接
 */
bool TcpServer_IsClientConnected(void);

/**
 * @brief 注册命令处理回调
 * @param handler 回调函数
 * @return 执行结果
 * @note 如果不注册，将使用默认处理器
 */
T_DjiReturnCode TcpServer_RegisterCommandHandler(TcpCommandHandler handler);

#ifdef __cplusplus
}
#endif

#endif // TCP_SERVER_HPP
