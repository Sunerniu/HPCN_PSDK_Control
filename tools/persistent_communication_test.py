#!/usr/bin/env python3
"""Maintain TCP and UDP heartbeats while interactively testing commands."""

import argparse
import json
import socket
import struct
import threading
import time


HEADER = 0xAA55
COMMANDS = {
    "takeoff": 0x01,
    "land": 0x02,
    "gohome": 0x03,
    "cancelhome": 0x04,
    "hover": 0x05,
    "confirmland": 0x06,
    "forceland": 0x07,
    "cancelland": 0x08,
    "plansto": 0x13,
    "changesto": 0x15,
    "heightto": 0x17,
    "start": 0x20,
    "stop": 0x21,
    "pause": 0x22,
    "resume": 0x23,
    "clear": 0x24,
    "status": 0x30,
    "auth": 0x31,
    "heartbeat": 0x3F,
    "gimbal_reset": 0x41,
}


def build_packet(
    command: str,
    latitude: float = 0.0,
    longitude: float = 0.0,
    altitude: float = 0.0,
    speed: float = 0.0,
) -> bytes:
    first_28 = struct.pack(
        "<HBBddff",
        HEADER,
        COMMANDS[command],
        0,
        latitude,
        longitude,
        altitude,
        speed,
    )
    return first_28 + struct.pack("<H", sum(first_28) & 0xFFFF)


def parse_command(line: str):
    parts = line.strip().lower().split()
    if not parts:
        return None
    if parts[0] in ("quit", "exit", "q"):
        return ("quit",)
    if len(parts) < 2 or parts[0] not in ("tcp", "udp"):
        raise ValueError("格式应为：tcp|udp 命令 [参数]")

    transport, command = parts[0], parts[1]
    if command not in COMMANDS or command == "heartbeat":
        raise ValueError(f"未知命令：{command}")

    values = [float(item) for item in parts[2:]]
    latitude = longitude = altitude = speed = 0.0
    if command in ("plansto", "changesto"):
        if len(values) not in (3, 4):
            raise ValueError(
                f"{command} 需要：纬度 经度 绝对高度 [速度]"
            )
        latitude, longitude, altitude = values[:3]
        speed = values[3] if len(values) == 4 else 0.0
    elif command == "heightto":
        if len(values) != 1:
            raise ValueError("heightto 需要一个绝对高度")
        altitude = values[0]
    elif values:
        raise ValueError(f"{command} 不接受参数")

    return (
        transport,
        command,
        build_packet(command, latitude, longitude, altitude, speed),
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, help="AGX IP")
    parser.add_argument("--tcp-port", type=int, default=8080)
    parser.add_argument("--udp-port", type=int, default=14551)
    parser.add_argument("--heartbeat-interval", type=float, default=1.0)
    args = parser.parse_args()

    stop_event = threading.Event()
    tcp_lock = threading.Lock()
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tcp_socket = socket.create_connection(
        (args.host, args.tcp_port), timeout=5
    )
    tcp_socket.settimeout(None)

    def heartbeat_loop() -> None:
        tcp_heartbeat = build_packet("heartbeat")
        while not stop_event.wait(args.heartbeat_interval):
            heartbeat_json = json.dumps(
                {
                    "msg_type": "heartbeat",
                    "timestamp": time.time_ns() // 1_000_000,
                    "status": "alive",
                },
                separators=(",", ":"),
            ).encode()
            try:
                udp_socket.sendto(
                    heartbeat_json, (args.host, args.udp_port)
                )
                with tcp_lock:
                    tcp_socket.sendall(tcp_heartbeat)
            except OSError as error:
                print(f"\n[ERROR] 心跳发送失败：{error}")
                stop_event.set()
                return

    heartbeat_thread = threading.Thread(
        target=heartbeat_loop, name="heartbeat", daemon=True
    )
    heartbeat_thread.start()

    print(f"TCP connected: {args.host}:{args.tcp_port}")
    print(f"UDP heartbeat: {args.host}:{args.udp_port}, every "
          f"{args.heartbeat_interval:.1f}s")
    print("示例：tcp status | udp status | udp takeoff | udp hover | udp land")
    print("高度：udp heightto 50")
    print("退出：quit（退出后心跳停止，AGX 将执行失联保护）")

    try:
        while not stop_event.is_set():
            try:
                parsed = parse_command(input("control> "))
            except ValueError as error:
                print(f"[ERROR] {error}")
                continue
            if parsed is None:
                continue
            if parsed[0] == "quit":
                break

            transport, command, packet = parsed
            if transport == "tcp":
                with tcp_lock:
                    tcp_socket.sendall(packet)
            else:
                udp_socket.sendto(packet, (args.host, args.udp_port))
            print(f"[OK] {command} sent through {transport.upper()}")
    except (EOFError, KeyboardInterrupt):
        print()
    finally:
        stop_event.set()
        heartbeat_thread.join(timeout=2)
        tcp_socket.close()
        udp_socket.close()
        print("Communication test stopped; TCP disconnected and UDP heartbeat stopped.")


if __name__ == "__main__":
    main()
