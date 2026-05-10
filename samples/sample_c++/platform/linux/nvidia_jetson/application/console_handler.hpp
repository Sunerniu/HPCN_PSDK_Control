/**
 ********************************************************************
 * @file    console_handler.hpp
 * @brief   命令处理模块 - 统一处理控制台和TCP命令
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 *********************************************************************
 */

#ifndef CONSOLE_HANDLER_HPP
#define CONSOLE_HANDLER_HPP

/* Includes ------------------------------------------------------------------*/
#include "command_control.hpp"

#ifdef __cplusplus
extern "C" {
#endif
#include "protocol.h"

/**
 * @brief 命令来源标识
 */
typedef enum {
  CMD_SOURCE_CONSOLE = 0, // 控制台输入
  CMD_SOURCE_TCP,         // TCP远程命令
  CMD_SOURCE_OTHER,       // 其他来源
} E_CommandSource;

/**
 * @brief 控制台处理器配置
 */
typedef struct {
  bool flightControlAvailable; // 飞控是否可用
} T_ConsoleConfig;

/* Exported functions --------------------------------------------------------*/

// ===== 控制台交互 =====

/**
 * @brief 初始化控制台处理器
 * @param config 配置参数
 * @return 执行结果
 */
T_DjiReturnCode ConsoleHandler_Init(const T_ConsoleConfig *config);

/**
 * @brief 运行命令处理循环 (阻塞)
 */
void ConsoleHandler_RunLoop(void);

/**
 * @brief 请求停止命令循环
 */
void ConsoleHandler_RequestStop(void);

/**
 * @brief 打印帮助信息
 */
void ConsoleHandler_PrintHelp(void);

// ===== 命令执行 (供TCP等外部调用) =====

/**
 * @brief 执行简单命令 (无参数)
 * @param cmdType 命令类型
 * @param source 命令来源
 * @return 执行结果
 */
T_DjiReturnCode ConsoleHandler_ExecuteCommand(E_CommandType cmdType,
                                              E_CommandSource source);

/**
 * @brief 从协议包执行命令 (TCP使用)
 * @param packet 协议包
 * @return 执行结果
 */
T_DjiReturnCode ConsoleHandler_ExecuteFromPacket(const T_ControlPacket *packet);

/**
 * @brief 执行导航命令
 * @param cmdType 命令类型
 * @param wp 航点参数
 * @param source 命令来源
 * @return 执行结果
 */
T_DjiReturnCode ConsoleHandler_ExecuteNavigation(E_CommandType cmdType,
                                                 const T_Waypoint *wp,
                                                 E_CommandSource source);

#ifdef __cplusplus
}
#endif

#endif // CONSOLE_HANDLER_HPP
