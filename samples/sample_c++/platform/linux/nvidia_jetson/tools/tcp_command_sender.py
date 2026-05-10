#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DJI 无人机控制命令 TCP 发送器

功能:
- 通过 TCP 发送控制命令到 Jetson AGX
- 支持所有控制命令 (起飞、降落、导航等)
- 交互式命令行界面

用法:
    python tcp_command_sender.py [--host HOST] [--port PORT]
    
示例:
    python tcp_command_sender.py --host 192.168.1.100 --port 8080
"""

import socket
import struct
import argparse
import sys
import time
from typing import Optional, Tuple

# ============================================================================
# 协议常量 (对应 protocol.h)
# ============================================================================
PROTOCOL_HEADER = 0xAA55
PROTOCOL_PACKET_SIZE = 30

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

# ============================================================================
# 命令类型 (对应 E_CommandType)
# ============================================================================
class CommandType:
    # 基础控制命令 0x01 - 0x0F
    TAKEOFF = 0x01
    LAND = 0x02
    GOHOME = 0x03
    CANCEL_GOHOME = 0x04
    HOVER = 0x05
    CONFIRM_LAND = 0x06
    FORCE_LAND = 0x07
    CANCEL_LAND = 0x08
    
    # 导航命令 0x10 - 0x1F
    PLANSTO = 0x13       # 追加航点 (队列)
    CHANGESTO = 0x15     # 切换目标点 (丢弃原有目标)
    
    # 连续导航控制 0x20 - 0x2F
    NAV_START = 0x20     # 启动连续导航
    NAV_STOP = 0x21      # 停止连续导航
    NAV_PAUSE = 0x22     # 暂停导航
    NAV_RESUME = 0x23    # 恢复导航
    NAV_CLEAR = 0x24     # 清除目标 (悬停)
    
    # 系统命令 0x30 - 0x3F
    STATUS = 0x30        # 请求状态
    AUTH = 0x31          # 获取控制权限
    HEARTBEAT = 0x3F     # 心跳包


# 命令名称映射
COMMAND_NAMES = {
    CommandType.TAKEOFF: "起飞 (TAKEOFF)",
    CommandType.LAND: "降落 (LAND)",
    CommandType.GOHOME: "返航 (GOHOME)",
    CommandType.CANCEL_GOHOME: "取消返航 (CANCEL_GOHOME)",
    CommandType.HOVER: "悬停 (HOVER)",
    CommandType.CONFIRM_LAND: "确认降落 (CONFIRM_LAND)",
    CommandType.FORCE_LAND: "强制降落 (FORCE_LAND)",
    CommandType.CANCEL_LAND: "取消降落 (CANCEL_LAND)",
    CommandType.PLANSTO: "追加航点 (PLANSTO)",
    CommandType.CHANGESTO: "切换目标点 (CHANGESTO)",
    CommandType.NAV_START: "启动导航 (NAV_START)",
    CommandType.NAV_STOP: "停止导航 (NAV_STOP)",
    CommandType.NAV_PAUSE: "暂停导航 (NAV_PAUSE)",
    CommandType.NAV_RESUME: "恢复导航 (NAV_RESUME)",
    CommandType.NAV_CLEAR: "清除目标 (NAV_CLEAR)",
    CommandType.STATUS: "请求状态 (STATUS)",
    CommandType.AUTH: "获取权限 (AUTH)",
    CommandType.HEARTBEAT: "心跳 (HEARTBEAT)",
}

# ============================================================================
# 数据包构建
# ============================================================================

def calculate_checksum(data: bytes) -> int:
    """计算校验和 (字节求和)"""
    return sum(data) & 0xFFFF


def build_control_packet(
    cmd_type: int,
    latitude: float = 0.0,
    longitude: float = 0.0,
    altitude: float = 0.0,
    speed: float = 0.0
) -> bytes:
    """
    构建控制命令包
    
    Args:
        cmd_type: 命令类型
        latitude: 纬度 (度)
        longitude: 经度 (度)
        altitude: 高度 (米, 相对起飞点)
        speed: 速度 (m/s)
    
    Returns:
        30字节的二进制数据包
    """
    # 先打包不包含校验和的数据
    data_without_checksum = struct.pack(
        '<HBBddff',
        PROTOCOL_HEADER,
        cmd_type,
        0,  # reserved
        latitude,
        longitude,
        altitude,
        speed
    )
    
    # 计算校验和
    checksum = calculate_checksum(data_without_checksum)
    
    # 完整数据包
    packet = struct.pack(
        PACKET_FORMAT,
        PROTOCOL_HEADER,
        cmd_type,
        0,  # reserved
        latitude,
        longitude,
        altitude,
        speed,
        checksum
    )
    
    return packet


# ============================================================================
# TCP 发送器类
# ============================================================================

class TcpCommandSender:
    """TCP 命令发送器"""
    
    def __init__(self, host: str, port: int, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.socket: Optional[socket.socket] = None
    
    def connect(self) -> bool:
        """连接到服务器"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(self.timeout)
            self.socket.connect((self.host, self.port))
            print(f"[OK] 已连接到 {self.host}:{self.port}")
            return True
        except socket.error as e:
            print(f"[ERROR] 连接失败: {e}")
            self.socket = None
            return False
    
    def disconnect(self):
        """断开连接"""
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
            print("[INFO] 已断开连接")
    
    def is_connected(self) -> bool:
        """检查是否已连接"""
        return self.socket is not None
    
    def send_packet(self, packet: bytes) -> bool:
        """发送数据包"""
        if not self.socket:
            print("[ERROR] 未连接到服务器")
            return False
        
        try:
            self.socket.sendall(packet)
            return True
        except socket.error as e:
            print(f"[ERROR] 发送失败: {e}")
            return False
    
    def send_command(
        self,
        cmd_type: int,
        latitude: float = 0.0,
        longitude: float = 0.0,
        altitude: float = 0.0,
        speed: float = 0.0
    ) -> bool:
        """发送控制命令"""
        packet = build_control_packet(cmd_type, latitude, longitude, altitude, speed)
        cmd_name = COMMAND_NAMES.get(cmd_type, f"未知命令(0x{cmd_type:02X})")
        
        if self.send_packet(packet):
            if cmd_type in (CommandType.PLANSTO, CommandType.CHANGESTO):
                print(f"[TX] {cmd_name}: lat={latitude:.7f}, lon={longitude:.7f}, alt={altitude:.1f}m, speed={speed:.1f}m/s")
            else:
                print(f"[TX] {cmd_name}")
            return True
        return False
    
    # ========== 便捷命令方法 ==========
    
    def takeoff(self) -> bool:
        """起飞"""
        return self.send_command(CommandType.TAKEOFF)
    
    def land(self) -> bool:
        """降落"""
        return self.send_command(CommandType.LAND)
    
    def go_home(self) -> bool:
        """返航"""
        return self.send_command(CommandType.GOHOME)
    
    def cancel_go_home(self) -> bool:
        """取消返航"""
        return self.send_command(CommandType.CANCEL_GOHOME)
    
    def hover(self) -> bool:
        """悬停"""
        return self.send_command(CommandType.HOVER)
    
    def confirm_land(self) -> bool:
        """确认降落"""
        return self.send_command(CommandType.CONFIRM_LAND)
    
    def force_land(self) -> bool:
        """强制降落"""
        return self.send_command(CommandType.FORCE_LAND)
    
    def cancel_land(self) -> bool:
        """取消降落"""
        return self.send_command(CommandType.CANCEL_LAND)
    
    def plansto(self, lat: float, lon: float, alt: float, speed: float = 5.0) -> bool:
        """追加航点到队列"""
        return self.send_command(CommandType.PLANSTO, lat, lon, alt, speed)
    
    def changesto(self, lat: float, lon: float, alt: float, speed: float = 5.0) -> bool:
        """切换目标点 (丢弃原有目标)"""
        return self.send_command(CommandType.CHANGESTO, lat, lon, alt, speed)
    
    def nav_start(self) -> bool:
        """启动连续导航"""
        return self.send_command(CommandType.NAV_START)
    
    def nav_stop(self) -> bool:
        """停止连续导航"""
        return self.send_command(CommandType.NAV_STOP)
    
    def nav_pause(self) -> bool:
        """暂停导航"""
        return self.send_command(CommandType.NAV_PAUSE)
    
    def nav_resume(self) -> bool:
        """恢复导航"""
        return self.send_command(CommandType.NAV_RESUME)
    
    def nav_clear(self) -> bool:
        """清除目标 (悬停)"""
        return self.send_command(CommandType.NAV_CLEAR)
    
    def request_status(self) -> bool:
        """请求状态"""
        return self.send_command(CommandType.STATUS)
    
    def auth(self) -> bool:
        """获取控制权限"""
        return self.send_command(CommandType.AUTH)
    
    def heartbeat(self) -> bool:
        """心跳"""
        return self.send_command(CommandType.HEARTBEAT)


# ============================================================================
# 交互式命令行界面
# ============================================================================

def print_help():
    """打印帮助信息"""
    print("""
================================================================================
                        DJI 无人机 TCP 控制命令
================================================================================
基础控制:
  takeoff         - 起飞
  land            - 降落
  home            - 返航
  cancel_home     - 取消返航
  hover           - 悬停
  confirm_land    - 确认降落
  force_land      - 强制降落
  cancel_land     - 取消降落

导航命令:
  plansto <lat> <lon> <alt> [speed]   - 追加航点到队列
  changesto <lat> <lon> <alt> [speed] - 切换目标点 (丢弃原有目标)
  nav_start       - 启动连续导航
  nav_stop        - 停止连续导航
  nav_pause       - 暂停导航
  nav_resume      - 恢复导航
  nav_clear       - 清除目标 (悬停)

系统命令:
  status          - 请求状态
  auth            - 获取控制权限
  heartbeat       - 心跳

其他:
  connect         - 重新连接
  disconnect      - 断开连接
  help            - 显示此帮助
  quit / exit     - 退出程序
================================================================================
""")


def parse_nav_command(parts: list) -> Tuple[Optional[float], Optional[float], Optional[float], float]:
    """解析导航命令参数"""
    if len(parts) < 4:
        print("[ERROR] 用法: plansto/changesto <纬度> <经度> <高度> [速度]")
        return None, None, None, 0.0
    
    try:
        lat = float(parts[1])
        lon = float(parts[2])
        alt = float(parts[3])
        speed = float(parts[4]) if len(parts) > 4 else 5.0
        return lat, lon, alt, speed
    except ValueError:
        print("[ERROR] 参数必须是数字")
        return None, None, None, 0.0


def interactive_mode(sender: TcpCommandSender):
    """交互式命令行模式"""
    print_help()
    
    while True:
        try:
            prompt = f"[{'连接' if sender.is_connected() else '未连接'}] > "
            cmd_input = input(prompt).strip().lower()
            
            if not cmd_input:
                continue
            
            parts = cmd_input.split()
            cmd = parts[0]
            
            # 退出
            if cmd in ('quit', 'exit', 'q'):
                print("[INFO] 正在退出...")
                break
            
            # 帮助
            if cmd in ('help', 'h', '?'):
                print_help()
                continue
            
            # 连接管理
            if cmd == 'connect':
                if sender.is_connected():
                    sender.disconnect()
                sender.connect()
                continue
            
            if cmd == 'disconnect':
                sender.disconnect()
                continue
            
            # 检查连接状态
            if not sender.is_connected():
                print("[WARN] 未连接到服务器, 使用 'connect' 命令连接")
                continue
            
            # 基础控制命令
            if cmd == 'takeoff':
                sender.takeoff()
            elif cmd == 'land':
                sender.land()
            elif cmd in ('home', 'gohome'):
                sender.go_home()
            elif cmd in ('cancel_home', 'cancelhome'):
                sender.cancel_go_home()
            elif cmd == 'hover':
                sender.hover()
            elif cmd in ('confirm_land', 'confirmland'):
                sender.confirm_land()
            elif cmd in ('force_land', 'forceland'):
                sender.force_land()
            elif cmd in ('cancel_land', 'cancelland'):
                sender.cancel_land()
            
            # 导航命令
            elif cmd == 'plansto':
                lat, lon, alt, speed = parse_nav_command(parts)
                if lat is not None:
                    sender.plansto(lat, lon, alt, speed)
            elif cmd == 'changesto':
                lat, lon, alt, speed = parse_nav_command(parts)
                if lat is not None:
                    sender.changesto(lat, lon, alt, speed)
            elif cmd in ('nav_start', 'navstart'):
                sender.nav_start()
            elif cmd in ('nav_stop', 'navstop'):
                sender.nav_stop()
            elif cmd in ('nav_pause', 'navpause'):
                sender.nav_pause()
            elif cmd in ('nav_resume', 'navresume'):
                sender.nav_resume()
            elif cmd in ('nav_clear', 'navclear'):
                sender.nav_clear()
            
            # 系统命令
            elif cmd == 'status':
                sender.request_status()
            elif cmd == 'auth':
                sender.auth()
            elif cmd == 'heartbeat':
                sender.heartbeat()
            
            else:
                print(f"[ERROR] 未知命令: {cmd}. 输入 'help' 查看帮助")
                
        except KeyboardInterrupt:
            print("\n[INFO] 按 Ctrl+C 退出, 输入 'quit' 正常退出")
        except EOFError:
            print("\n[INFO] 输入结束, 退出...")
            break


def main():
    parser = argparse.ArgumentParser(
        description='DJI 无人机控制命令 TCP 发送器',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
示例:
  %(prog)s --host 192.168.1.100            # 连接到 192.168.1.100:8080
  %(prog)s --host 192.168.1.100 --port 8888  # 指定端口
  %(prog)s --cmd takeoff                    # 单次发送起飞命令
  %(prog)s --cmd plansto --lat 30.0 --lon 120.0 --alt 50  # 发送导航命令
        '''
    )
    parser.add_argument('--host', '-H', type=str, default='127.0.0.1',
                        help='AGX 服务器地址 (默认: 127.0.0.1)')
    parser.add_argument('--port', '-p', type=int, default=8080,
                        help='AGX 服务器端口 (默认: 8080)')
    parser.add_argument('--timeout', '-t', type=float, default=5.0,
                        help='连接超时 (秒, 默认: 5.0)')
    
    # 单次命令模式
    parser.add_argument('--cmd', '-c', type=str, default=None,
                        help='单次发送命令 (不进入交互模式)')
    parser.add_argument('--lat', type=float, default=0.0,
                        help='纬度 (用于导航命令)')
    parser.add_argument('--lon', type=float, default=0.0,
                        help='经度 (用于导航命令)')
    parser.add_argument('--alt', type=float, default=0.0,
                        help='高度 (米, 用于导航命令)')
    parser.add_argument('--speed', type=float, default=5.0,
                        help='速度 (m/s, 用于导航命令, 默认: 5.0)')
    
    args = parser.parse_args()
    
    # 创建发送器
    sender = TcpCommandSender(args.host, args.port, args.timeout)
    
    # 尝试连接
    if not sender.connect():
        print("[ERROR] 无法连接到服务器")
        sys.exit(1)
    
    try:
        # 单次命令模式
        if args.cmd:
            cmd = args.cmd.lower()
            cmd_map = {
                'takeoff': sender.takeoff,
                'land': sender.land,
                'home': sender.go_home,
                'gohome': sender.go_home,
                'cancel_home': sender.cancel_go_home,
                'hover': sender.hover,
                'confirm_land': sender.confirm_land,
                'force_land': sender.force_land,
                'cancel_land': sender.cancel_land,
                'nav_start': sender.nav_start,
                'nav_stop': sender.nav_stop,
                'nav_pause': sender.nav_pause,
                'nav_resume': sender.nav_resume,
                'nav_clear': sender.nav_clear,
                'status': sender.request_status,
                'auth': sender.auth,
                'heartbeat': sender.heartbeat,
            }
            
            if cmd == 'plansto':
                sender.plansto(args.lat, args.lon, args.alt, args.speed)
            elif cmd == 'changesto':
                sender.changesto(args.lat, args.lon, args.alt, args.speed)
            elif cmd in cmd_map:
                cmd_map[cmd]()
            else:
                print(f"[ERROR] 未知命令: {cmd}")
                sys.exit(1)
        else:
            # 交互模式
            interactive_mode(sender)
    
    finally:
        sender.disconnect()


if __name__ == '__main__':
    main()
