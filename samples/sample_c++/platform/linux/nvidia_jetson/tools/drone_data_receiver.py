#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
DJI 无人机数据 UDP 接收器

功能:
- 接收来自 Jetson 的无人机状态数据 (UDP JSON)
- 实时显示无人机状态
- 可选保存到文件

用法:
    python drone_data_receiver.py [--port PORT] [--save FILE]
    
示例:
    python drone_data_receiver.py --port 14550
    python drone_data_receiver.py --port 14550 --save flight_data.json
"""

import socket
import json
import argparse
import sys
import time
from datetime import datetime

# 默认配置
DEFAULT_PORT = 14550
DEFAULT_BUFFER_SIZE = 4096


def get_flight_status_string(status: int) -> str:
    """获取飞行状态描述"""
    status_map = {
        0: "停止 (电机静止)",
        1: "地面 (电机旋转)",
        2: "空中飞行"
    }
    return status_map.get(status, f"未知状态({status})")


def get_display_mode_string(mode: int) -> str:
    """获取显示模式描述"""
    mode_map = {
        1: "手动控制",
        2: "姿态模式",
        6: "GPS模式",
        9: "热点模式",
        10: "辅助起飞",
        11: "自动起飞",
        12: "自动降落",
        15: "自动返航",
        17: "SDK控制",
        33: "强制降落",
        68: "搜索模式",
        100: "电机启动"
    }
    return mode_map.get(mode, f"未知模式({mode})")


def format_relative_time(timestamp: dict) -> str:
    """格式化 PSDK 订阅数据的相对时间戳"""
    if not isinstance(timestamp, dict):
        return "无数据"

    milliseconds = timestamp.get('ms')
    microseconds = timestamp.get('us')
    if milliseconds is None or microseconds is None:
        return "无数据"

    return f"{milliseconds} ms + {microseconds} us"


def format_real_time(real_time: dict) -> str:
    """格式化 PPS 映射后的 UTC 现实时间"""
    if not isinstance(real_time, dict) or not real_time.get('valid', False):
        return "未同步（等待 PPS/时间映射）"

    iso8601 = real_time.get('iso8601', '未提供')
    epoch_us = real_time.get('epoch_us')
    if epoch_us is None:
        return iso8601

    return f"{iso8601}  (epoch_us: {epoch_us})"


def append_position_block(lines: list, name: str, position: dict) -> None:
    """输出 GPS 或 RTK 位置及时间同步状态"""
    if not isinstance(position, dict):
        return

    valid = bool(position.get('valid', False))
    lines.append(f"\n【{name}位置】")
    lines.append(f"  位置有效: {'是' if valid else '否'}")
    lines.append(f"  纬度: {position.get('lat', 0):.7f}°")
    lines.append(f"  经度: {position.get('lon', 0):.7f}°")
    lines.append(f"  高度: {position.get('alt', 0):.2f} m")

    if name == 'GPS':
        lines.append(f"  Fix State: {position.get('fix_state', '未知')}")
        details_time = position.get('details_time')
        if details_time is not None:
            lines.append(f"  详情相对时间: {format_relative_time(details_time)}")
    elif name == 'RTK':
        lines.append(f"  Position Info: {position.get('position_info', '未知')}")
        info_time = position.get('info_time')
        if info_time is not None:
            lines.append(f"  状态相对时间: {format_relative_time(info_time)}")

    lines.append(
        f"  位置相对时间: {format_relative_time(position.get('position_time'))}"
    )
    lines.append(
        f"  位置现实时间: {format_real_time(position.get('position_real_time'))}"
    )


def format_drone_status(data: dict) -> str:
    """格式化无人机状态为可读字符串"""
    lines = []
    lines.append("\n" + "=" * 50)
    lines.append(f"  无人机状态 - {datetime.now().strftime('%H:%M:%S')}")
    lines.append("=" * 50)
    
    # 姿态信息
    if 'attitude' in data:
        att = data['attitude']
        lines.append("\n【姿态信息】")
        lines.append(f"  俯仰角 (Pitch): {att.get('pitch', 0):.2f}°")
        lines.append(f"  横滚角 (Roll):  {att.get('roll', 0):.2f}°")
        lines.append(f"  偏航角 (Yaw):   {att.get('yaw', 0):.2f}°")
    
    # PPS 版本的 GPS/RTK 位置与时间信息
    position_source = data.get('position_source')
    if position_source is not None:
        lines.append(f"\n  当前位置源: {position_source}")

    append_position_block(lines, 'GPS', data.get('gps'))
    append_position_block(lines, 'RTK', data.get('rtk'))

    # 兼容旧版 position 字段
    if 'position' in data:
        pos = data['position']
        lines.append("\n【旧版融合位置】")
        lines.append(f"  纬度: {pos.get('lat', 0):.7f}°")
        lines.append(f"  经度: {pos.get('lon', 0):.7f}°")
        lines.append(f"  高度: {pos.get('alt', 0):.2f} m")
    
    if 'position_fused' in data:
        fused = data['position_fused']
        lines.append(f"  卫星数量: {fused.get('satellites', 0)}")
    
    if 'height' in data:
        lines.append(f"  融合高度: {data['height']:.2f} m")
    
    # 速度信息
    if 'velocity' in data:
        vel = data['velocity']
        lines.append("\n【速度信息】")
        lines.append(f"  Vx: {vel.get('vx', 0):.2f} m/s")
        lines.append(f"  Vy: {vel.get('vy', 0):.2f} m/s")
        lines.append(f"  Vz: {vel.get('vz', 0):.2f} m/s")
    
    # 飞行状态
    lines.append("\n【飞行状态】")
    lines.append(f"  飞行状态: {get_flight_status_string(data.get('flight_status', 0))}")
    lines.append(f"  显示模式: {get_display_mode_string(data.get('display_mode', 0))}")
    
    # 电池信息
    if 'battery' in data:
        bat = data['battery']
        lines.append("\n【电池信息】")
        lines.append(f"  电压: {bat.get('voltage', 0)} mV")
        lines.append(f"  电流: {bat.get('current', 0)} mA")
        lines.append(f"  电量: {bat.get('percent', 0)}%")
    
    # 返航点
    home_set = data.get('home_set', 0)
    lines.append("\n【返航点】")
    lines.append(f"  返航点设置: {'已设置' if home_set == 1 else '未设置'}")
    
    lines.append("=" * 50)
    
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description='DJI 无人机数据 UDP 接收器',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
示例:
  %(prog)s                          # 使用默认端口 14550
  %(prog)s --port 14551             # 使用端口 14551
  %(prog)s --port 14550 --save log.json  # 保存到文件
  %(prog)s --raw                    # 显示原始 JSON
        '''
    )
    parser.add_argument('--port', '-p', type=int, default=DEFAULT_PORT,
                        help=f'监听端口 (默认: {DEFAULT_PORT})')
    parser.add_argument('--save', '-s', type=str, default=None,
                        help='保存数据到文件 (JSON Lines 格式)')
    parser.add_argument('--raw', '-r', action='store_true',
                        help='显示原始 JSON 数据')
    parser.add_argument('--bind', '-b', type=str, default='0.0.0.0',
                        help='绑定地址 (默认: 0.0.0.0)')
    
    args = parser.parse_args()
    
    # 创建 UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        sock.bind((args.bind, args.port))
    except OSError as e:
        print(f"[ERROR] 无法绑定到 {args.bind}:{args.port} - {e}", file=sys.stderr)
        sys.exit(1)
    
    print(f"[INFO] 正在监听 UDP 端口 {args.port}...")
    print(f"[INFO] 按 Ctrl+C 退出\n")
    
    # 打开保存文件
    save_file = None
    if args.save:
        try:
            save_file = open(args.save, 'a', encoding='utf-8')
            print(f"[INFO] 数据将保存到: {args.save}")
        except IOError as e:
            print(f"[WARN] 无法打开保存文件: {e}", file=sys.stderr)
    
    packet_count = 0
    last_time = time.time()
    
    try:
        while True:
            try:
                data, addr = sock.recvfrom(DEFAULT_BUFFER_SIZE)
                packet_count += 1
                
                # 解码 JSON
                try:
                    json_data = json.loads(data.decode('utf-8'))
                except json.JSONDecodeError as e:
                    print(f"[WARN] JSON 解析错误: {e}")
                    continue
                
                # 保存到文件
                if save_file:
                    json_data['_recv_time'] = datetime.now().isoformat()
                    json_data['_source'] = f"{addr[0]}:{addr[1]}"
                    save_file.write(json.dumps(json_data, ensure_ascii=False) + '\n')
                    save_file.flush()
                
                # 显示数据
                if args.raw:
                    print(json.dumps(json_data, indent=2, ensure_ascii=False))
                else:
                    # 清屏并显示格式化状态 (每秒最多更新一次显示)
                    current_time = time.time()
                    if current_time - last_time >= 0.5:
                        # 使用 ANSI 转义序列清屏
                        print("\033[2J\033[H", end="")
                        print(format_drone_status(json_data))
                        print(f"\n[统计] 已接收: {packet_count} 包 | 来源: {addr[0]}:{addr[1]}")
                        last_time = current_time
                
            except socket.timeout:
                continue
                
    except KeyboardInterrupt:
        print("\n\n[INFO] 用户中断，正在退出...")
    finally:
        sock.close()
        if save_file:
            save_file.close()
            print(f"[INFO] 数据已保存到: {args.save}")
        print(f"[INFO] 共接收 {packet_count} 个数据包")


if __name__ == '__main__':
    main()
