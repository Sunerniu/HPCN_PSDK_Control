#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TCP 控制发送器 - PC端发送控制命令到AGX

用法:
    python ground_sender.py --ip 192.168.1.100 --port 8080

Copyright (c) 2024 DJI. All rights reserved.
"""

import socket
import struct
import argparse
import sys
import time

# ============================================================================
# 协议定义 (与 protocol.h 保持一致)
# ============================================================================

PROTOCOL_HEADER = 0xAA55

# 协议包格式: '<HBBddffH' (小端序)
# H: header (2 bytes)
# B: cmdType (1 byte)
# B: reserved (1 byte)
# d: latitude (8 bytes, double)
# d: longitude (8 bytes, double)
# f: altitude (4 bytes, float)
# f: speed (4 bytes, float)
# H: checksum (2 bytes)
PACKET_FORMAT = '<HBBddffH'
PACKET_SIZE = struct.calcsize(PACKET_FORMAT)  # 30 bytes

# 命令类型 (与 E_CommandType 保持一致)
CMD_TAKEOFF       = 0x01
CMD_LAND          = 0x02
CMD_GOHOME        = 0x03
CMD_CANCEL_GOHOME = 0x04
CMD_HOVER         = 0x05
CMD_GOTO          = 0x10
CMD_FLYTO         = 0x11
CMD_NAV_START     = 0x20
CMD_NAV_STOP      = 0x21
CMD_NAV_PAUSE     = 0x22
CMD_NAV_RESUME    = 0x23
CMD_NAV_CLEAR     = 0x24
CMD_STATUS        = 0x30
CMD_AUTH          = 0x31
CMD_HEARTBEAT     = 0x3F

# 命令名称映射
CMD_NAMES = {
    CMD_TAKEOFF: "起飞",
    CMD_LAND: "降落",
    CMD_GOHOME: "返航",
    CMD_CANCEL_GOHOME: "取消返航",
    CMD_HOVER: "悬停",
    CMD_GOTO: "飞往目标 (连续)",
    CMD_FLYTO: "飞往目标 (阻塞)",
    CMD_NAV_START: "启动导航",
    CMD_NAV_STOP: "停止导航",
    CMD_NAV_PAUSE: "暂停导航",
    CMD_NAV_RESUME: "恢复导航",
    CMD_NAV_CLEAR: "清除目标",
    CMD_STATUS: "查询状态",
    CMD_AUTH: "获取权限",
    CMD_HEARTBEAT: "心跳",
}


# ============================================================================
# 协议函数
# ============================================================================

def calculate_checksum(cmd_type: int, lat: float, lon: float, alt: float, speed: float) -> int:
    """计算校验和 (简单求和)"""
    checksum = cmd_type
    checksum += int(lat * 1e6) & 0xFFFFFFFF
    checksum += int(lon * 1e6) & 0xFFFFFFFF
    checksum += int(alt * 100) & 0xFFFFFFFF
    checksum += int(speed * 100) & 0xFFFFFFFF
    return checksum & 0xFFFF


def create_packet(cmd_type: int, lat: float = 0.0, lon: float = 0.0, 
                  alt: float = 0.0, speed: float = 0.0) -> bytes:
    """创建控制数据包"""
    checksum = calculate_checksum(cmd_type, lat, lon, alt, speed)
    packet = struct.pack(PACKET_FORMAT, 
                        PROTOCOL_HEADER, 
                        cmd_type, 
                        0,  # reserved
                        lat, 
                        lon, 
                        alt, 
                        speed, 
                        checksum)
    return packet


def send_command(sock: socket.socket, cmd_type: int, 
                 lat: float = 0.0, lon: float = 0.0, 
                 alt: float = 0.0, speed: float = 0.0) -> bool:
    """发送控制命令"""
    try:
        packet = create_packet(cmd_type, lat, lon, alt, speed)
        sock.sendall(packet)
        cmd_name = CMD_NAMES.get(cmd_type, f"0x{cmd_type:02X}")
        print(f"[发送] {cmd_name}", end="")
        if cmd_type in [CMD_GOTO, CMD_FLYTO]:
            print(f" lat={lat:.6f}, lon={lon:.6f}, alt={alt:.1f}m, speed={speed:.1f}m/s")
        else:
            print()
        return True
    except Exception as e:
        print(f"[错误] 发送失败: {e}")
        return False


# ============================================================================
# 命令解析
# ============================================================================

def parse_goto_command(args: str) -> tuple:
    """解析 goto/flyto 命令参数: lat,lon,alt[,speed]"""
    parts = args.split(',')
    if len(parts) < 3:
        raise ValueError("格式: lat,lon,alt[,speed]")
    
    lat = float(parts[0].strip())
    lon = float(parts[1].strip())
    alt = float(parts[2].strip())
    speed = float(parts[3].strip()) if len(parts) > 3 else 3.0
    
    return lat, lon, alt, speed


def process_command(sock: socket.socket, cmd_input: str) -> bool:
    """处理用户输入的命令"""
    cmd_input = cmd_input.strip().lower()
    
    if not cmd_input:
        return True
    
    # 简单命令
    simple_commands = {
        'takeoff': CMD_TAKEOFF,
        't': CMD_TAKEOFF,
        'land': CMD_LAND,
        'l': CMD_LAND,
        'home': CMD_GOHOME,
        'gohome': CMD_GOHOME,
        'cancelhome': CMD_CANCEL_GOHOME,
        'hover': CMD_HOVER,
        'navstart': CMD_NAV_START,
        'navstop': CMD_NAV_STOP,
        'navpause': CMD_NAV_PAUSE,
        'navresume': CMD_NAV_RESUME,
        'navclear': CMD_NAV_CLEAR,
        'status': CMD_STATUS,
        'auth': CMD_AUTH,
        'heartbeat': CMD_HEARTBEAT,
    }
    
    if cmd_input in simple_commands:
        return send_command(sock, simple_commands[cmd_input])
    
    # goto 命令
    if cmd_input.startswith('goto '):
        try:
            lat, lon, alt, speed = parse_goto_command(cmd_input[5:])
            return send_command(sock, CMD_GOTO, lat, lon, alt, speed)
        except ValueError as e:
            print(f"[错误] {e}")
            return True
    
    # flyto 命令
    if cmd_input.startswith('flyto '):
        try:
            lat, lon, alt, speed = parse_goto_command(cmd_input[6:])
            return send_command(sock, CMD_FLYTO, lat, lon, alt, speed)
        except ValueError as e:
            print(f"[错误] {e}")
            return True
    
    # 帮助
    if cmd_input in ['help', 'h', '?']:
        print_help()
        return True
    
    # 退出
    if cmd_input in ['quit', 'exit', 'q']:
        return False
    
    print(f"[警告] 未知命令: {cmd_input}, 输入 'help' 查看帮助")
    return True


def print_help():
    """打印帮助信息"""
    print("""
==================== 命令列表 ====================
【基础控制】
  takeoff, t       - 起飞
  land, l          - 降落
  home             - 返航
  cancelhome       - 取消返航
  hover            - 悬停

【连续导航】 (50Hz平滑控制, 目标可随时更新)
  navstart         - 启动连续导航
  navstop          - 停止连续导航
  navpause         - 暂停导航 (悬停)
  navresume        - 恢复导航
  navclear         - 清除目标 (悬停)
  goto lat,lon,alt[,speed]  - 发送目标点
    例: goto 22.5431,113.9467,30
    例: goto 22.5431,113.9467,30,5

【阻塞式航点】 (等待到达后返回)
  flyto lat,lon,alt[,speed] - 飞往目标点

【系统】
  status           - 查询状态
  auth             - 获取控制权限
  heartbeat        - 发送心跳包
  help, h          - 显示帮助
  quit, exit       - 退出程序
==================================================
""")


# ============================================================================
# 主程序
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='TCP 控制发送器 - PC端发送控制命令到AGX',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python ground_sender.py --ip 192.168.1.100
  python ground_sender.py --ip 192.168.1.100 --port 8080

连接后输入 'help' 查看可用命令
        """
    )
    parser.add_argument('--ip', '-i', type=str, default='192.168.1.100',
                        help='AGX IP 地址 (默认: 192.168.1.100)')
    parser.add_argument('--port', '-p', type=int, default=8080,
                        help='TCP 端口 (默认: 8080)')
    
    args = parser.parse_args()
    
    print("=" * 50)
    print("    DJI Payload SDK TCP 控制发送器")
    print("=" * 50)
    print(f"目标地址: {args.ip}:{args.port}")
    print(f"数据包大小: {PACKET_SIZE} 字节")
    print()
    
    # 连接到 AGX
    print(f"[INFO] 正在连接到 {args.ip}:{args.port}...")
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)  # 10秒连接超时
        sock.connect((args.ip, args.port))
        sock.settimeout(None)  # 连接后取消超时
        print("[OK] 连接成功!")
        print()
    except socket.timeout:
        print(f"[错误] 连接超时 - 请检查 AGX 是否启动并监听端口 {args.port}")
        return 1
    except ConnectionRefusedError:
        print(f"[错误] 连接被拒绝 - 请检查 AGX TCP 服务器是否启动")
        return 1
    except Exception as e:
        print(f"[错误] 连接失败: {e}")
        return 1
    
    print_help()
    
    # 命令循环
    try:
        running = True
        while running:
            try:
                cmd = input(">>> ")
                running = process_command(sock, cmd)
            except KeyboardInterrupt:
                print("\n[INFO] Ctrl+C 收到, 退出...")
                break
            except EOFError:
                print("\n[INFO] EOF 收到, 退出...")
                break
    finally:
        sock.close()
        print("[INFO] 连接已关闭")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
