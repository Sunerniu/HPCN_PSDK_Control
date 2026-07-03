/**
 * ********************************************************************
 * @file    protocol.h
 * @brief   TCP通信协议定义 - AGX与PC之间的控制命令协议
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *
 * *********************************************************************
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

/* Includes ------------------------------------------------------------------*/
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported constants --------------------------------------------------------*/
#define PROTOCOL_HEADER 0xAA55  // 协议包头
#define PROTOCOL_PACKET_SIZE 30 // 数据包大小 (字节)

/* Exported types ------------------------------------------------------------*/

/**
 * @brief 控制命令类型
 */
typedef enum {
  // 基础控制命令 0x01 - 0x0F
  CMD_TAKEOFF = 0x01,       // 起飞
  CMD_LAND = 0x02,          // 降落
  CMD_GOHOME = 0x03,        // 返航
  CMD_CANCEL_GOHOME = 0x04, // 取消返航
  CMD_HOVER = 0x05,         // 悬停
  CMD_CONFIRM_LAND = 0x06,  // 确认降落
  CMD_FORCE_LAND = 0x07,    // 强制降落
  CMD_CANCEL_LAND = 0x08,   // 取消降落

  // 导航命令 0x10 - 0x1F
  CMD_PLANSTO = 0x13,   // 追加航点 (队列)
  CMD_CHANGESTO = 0x15, // 切换目标点 (丢弃原有目标)

  // 连续导航控制 0x20 - 0x2F
  CMD_NAV_START = 0x20,  // 启动或继续导航
  CMD_NAV_STOP = 0x21,   // 暂停导航，可恢复
  CMD_NAV_PAUSE = 0x22,  // 暂停导航
  CMD_NAV_RESUME = 0x23, // 恢复导航
  CMD_NAV_CLEAR = 0x24,  // 清空导航任务 (悬停)

  // 系统命令 0x30 - 0x3F
  CMD_STATUS = 0x30,    // 请求状态
  CMD_AUTH = 0x31,      // 获取控制权限
  CMD_HEARTBEAT = 0x3F, // 心跳包
  
  // 云台命令 0x40 - 0x4F
  CMD_GIMBAL_ROTATE = 0x40, // 云台旋转
  CMD_GIMBAL_RESET = 0x41,  // 云台重置
} E_CommandType;

#pragma pack(push, 1) // 1字节对齐，确保与Python struct兼容

/**
 * @brief 控制命令包 (PC -> AGX)
 *
 * 总大小: 2 + 1 + 1 + 8 + 8 + 4 + 4 + 2 = 30 字节
 *
 * Python struct 格式: '<HBBddffH' (小端序)
 */
typedef struct {
  uint16_t header;   // 包头 0xAA55
  uint8_t cmdType;   // 命令类型 (E_CommandType)
  uint8_t reserved;  // 保留字节 (用于对齐和未来扩展)
  double latitude;   // 纬度 (度), 用于导航命令
  double longitude;  // 经度 (度), 用于导航命令
  float altitude;    // 绝对高度 (米)
  float speed;       // 速度 (m/s)
  uint16_t checksum; // 校验和
} T_ControlPacket;

#pragma pack(pop)

/* Exported functions --------------------------------------------------------*/

/**
 * @brief 计算校验和
 * @param packet 数据包指针
 * @return 校验和值
 */
static inline uint16_t
Protocol_CalculateChecksum(const T_ControlPacket *packet) {
  uint16_t sum = 0;
  const uint8_t *data = (const uint8_t *)packet;
  // 计算除校验和字段外的所有字节
  for (int i = 0; i < (int)(sizeof(T_ControlPacket) - sizeof(uint16_t)); i++) {
    sum += data[i];
  }
  return sum;
}

/**
 * @brief 验证数据包
 * @param packet 数据包指针
 * @return 1 = 有效, 0 = 无效
 */
static inline int Protocol_ValidatePacket(const T_ControlPacket *packet) {
  if (packet == NULL)
    return 0;
  if (packet->header != PROTOCOL_HEADER)
    return 0;
  // 校验和验证 (可选, 为了兼容性先不强制)
  // if (packet->checksum != Protocol_CalculateChecksum(packet)) return 0;
  return 1;
}

/**
 * @brief 获取命令类型名称
 * @param cmdType 命令类型
 * @return 命令名称字符串
 */
static inline const char *Protocol_GetCommandName(uint8_t cmdType) {
  switch (cmdType) {
  case CMD_TAKEOFF:
    return "TAKEOFF";
  case CMD_LAND:
    return "LAND";
  case CMD_GOHOME:
    return "GOHOME";
  case CMD_CANCEL_GOHOME:
    return "CANCEL_GOHOME";
  case CMD_HOVER:
    return "HOVER";
  case CMD_CONFIRM_LAND:
    return "CONFIRM_LAND";
  case CMD_FORCE_LAND:
    return "FORCE_LAND";
  case CMD_CANCEL_LAND:
    return "CANCEL_LAND";
  case CMD_PLANSTO:
    return "PLANSTO";
  case CMD_CHANGESTO:
    return "CHANGESTO";
  case CMD_NAV_START:
    return "NAV_START";
  case CMD_NAV_STOP:
    return "NAV_STOP";
  case CMD_NAV_PAUSE:
    return "NAV_PAUSE";
  case CMD_NAV_RESUME:
    return "NAV_RESUME";
  case CMD_NAV_CLEAR:
    return "NAV_CLEAR";
  case CMD_STATUS:
    return "STATUS";
  case CMD_AUTH:
    return "AUTH";
  case CMD_HEARTBEAT:
    return "HEARTBEAT";
  case CMD_GIMBAL_ROTATE:
    return "GIMBAL_ROTATE";
  case CMD_GIMBAL_RESET:
    return "GIMBAL_RESET";
  default:
    return "UNKNOWN";
  }
}

#ifdef __cplusplus
}
#endif

#endif // PROTOCOL_H
