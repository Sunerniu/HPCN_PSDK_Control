/**
 ********************************************************************
 * @file    main.cpp
 * @brief   无人机控制程序入口 - 高度模块化设计
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 *********************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include <csignal>
#include <iostream>

#include "application.hpp"
#include "command_control.hpp"
#include "console_handler.hpp"
#include "data.hpp"
#include "gimbal_control.hpp"
#include "tcp_server.hpp"
#include "time_sync_bridge.hpp"
#include "udp_receiver.hpp"
#include "udp_sender.hpp"

#include <dji_logger.h>
#include <dji_platform.h>

/* Private functions declaration ---------------------------------------------*/
static void SignalHandler(int signalNum);
static void OnNavigationArrived(uint32_t targetIndex);

/* Exported functions definition ---------------------------------------------*/
int main(int argc, char **argv) {
  T_DjiReturnCode returnCode;
  T_DjiOsalHandler *osalHandler;
  T_CommandControlConfig cmdConfig;
  T_ConsoleConfig consoleConfig;
  bool flightControlAvailable = false;

  // 初始化应用程序
  try {
    Application application(argc, argv);
  } catch (const std::exception &e) {
    std::cerr << "Application initialization failed: " << e.what() << std::endl;
    return -1;
  }

  osalHandler = DjiPlatform_GetOsalHandler();
  if (osalHandler == NULL) {
    std::cerr << "Get OSAL handler failed" << std::endl;
    return -1;
  }

  // 注册信号处理
  signal(SIGINT, SignalHandler);
  signal(SIGTERM, SignalHandler);

  std::cout << "\n============================================" << std::endl;
  std::cout << "  DJI Payload SDK " << std::endl;
  std::cout << "============================================\n" << std::endl;

  // 1. 初始化 PPS 时间同步模块
  std::cout << "[INFO] 初始化 PPS 时间同步模块..." << std::endl;
  returnCode = TimeSyncBridge_Init();
  if (returnCode == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    std::cout << "[OK] PPS 时间同步模块初始化成功" << std::endl;
  } else {
    std::cerr << "[WARN] PPS 时间同步不可用，请检查 PPS_GPIOCHIP 和 PPS_GPIO_LINE 配置" << std::endl;
  }

  // 2. 初始化数据订阅模块
  std::cout << "[INFO] 初始化数据订阅模块..." << std::endl;
  returnCode = DataSubscriber_Init();
  if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    std::cerr << "[ERROR] 数据订阅模块初始化失败" << std::endl;
    TimeSyncBridge_DeInit();
    return -1;
  }
  DataSubscriber_SubscribeTopics();
  std::cout << "[OK] 数据订阅模块初始化成功" << std::endl;

  // 2. 初始化飞行控制模块
  std::cout << "[INFO] 初始化飞行控制模块..." << std::endl;
  cmdConfig.ridLatitude = 22.542812;
  cmdConfig.ridLongitude = 113.958902;
  cmdConfig.ridAltitude = 0;

  returnCode = CommandControl_Init(&cmdConfig);
  if (returnCode == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    std::cout << "[OK] 飞行控制模块初始化成功" << std::endl;
    flightControlAvailable = true;
  } else {
    std::cerr << "[WARN] 飞行控制模块初始化失败" << std::endl;
  }

  // 3. 初始化云台控制模块
  std::cout << "[INFO] 初始化云台控制模块..." << std::endl;
  returnCode = GimbalControl_Init();
  if (returnCode == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
      std::cout << "[OK] 云台控制模块初始化成功" << std::endl;
  } else {
      std::cerr << "[WARN] 云台控制模块初始化预警 (可能是未挂载云台设备) 返回码: 0x" << std::hex << returnCode << std::dec << std::endl;
  }

  // 4. 初始化通信模块
  std::cout << "[INFO] 初始化通信模块 " << std::endl;
  if (UdpSender_InitDefault() == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    UdpSender_StartStatusThread();
    std::cout << "[OK] UDP发送服务已启动 (Host: " << UDP_DEFAULT_IP << ":"
              << UDP_DEFAULT_PORT << ")" << std::endl;
  }
  if (TcpServer_InitAndStart() == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    std::cout << "[OK] TCP服务器已启动 (Listening on Port 8080)" << std::endl;
  }
  if (UdpReceiver_InitDefault() == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    UdpReceiver_Start();
    std::cout << "[OK] UDP命令接收已启动 (Listening on Port "
              << UDP_RECEIVER_DEFAULT_PORT << ")" << std::endl;
  }

  // 5. 初始化控制台处理器
  consoleConfig.flightControlAvailable = flightControlAvailable;
  ConsoleHandler_Init(&consoleConfig);

  osalHandler->TaskSleepMs(500);

  std::cout << "\n========================================" << std::endl;
  std::cout << "  系统初始化完成, 输入 'help' 查看命令" << std::endl;
  std::cout << "========================================\n" << std::endl;

  // 主线程运行命令循环
  ConsoleHandler_RunLoop();

  // 清理
  std::cout << "\n[INFO] 正在关闭..." << std::endl;

  UdpSender_StopStatusThread();
  UdpSender_DeInit();
  TcpServer_Stop();
  TcpServer_DeInit();
  UdpReceiver_Stop();
  UdpReceiver_DeInit();
  if (flightControlAvailable) {
    CommandControl_DeInit();
  }
  GimbalControl_DeInit();
  DataSubscriber_DeInit();
  TimeSyncBridge_DeInit();

  std::cout << "[OK] 程序已退出" << std::endl;
  return 0;
}

/* Private functions definition ----------------------------------------------*/

static void OnNavigationArrived(uint32_t targetIndex) {
  std::cout << "\n[EVENT] 已到达目标点 #" << targetIndex << std::endl;
}

static void SignalHandler(int signalNum) {
  (void)signalNum;
  std::cout << "\n[INFO] 收到退出信号..." << std::endl;
  ConsoleHandler_RequestStop();
}

/************************ (C) COPYRIGHT DJI Innovations *****END OF FILE****/
