#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Interactive TCP command sender for HPCN PSDK Control.

Default target: 10.129.155.64:8080
Packet format: <HBBddffH>, 30 bytes, same as protocol.h.
"""

import argparse
import shlex
import socket
import struct
import sys
from typing import List, Optional, Tuple


DEFAULT_HOST = "10.129.155.64"
DEFAULT_PORT = 8080
PROTOCOL_HEADER = 0xAA55
PACKET_FORMAT = "<HBBddffH"


COMMANDS = {
    "takeoff": 0x01,
    "land": 0x02,
    "gohome": 0x03,
    "home": 0x03,
    "cancel_gohome": 0x04,
    "cancelhome": 0x04,
    "hover": 0x05,
    "confirm_land": 0x06,
    "confirmland": 0x06,
    "force_land": 0x07,
    "forceland": 0x07,
    "cancel_land": 0x08,
    "cancelland": 0x08,
    "plansto": 0x13,
    "changesto": 0x15,
    "ascend": 0x16,
    "nav_start": 0x20,
    "start": 0x20,
    "nav_stop": 0x21,
    "stop": 0x21,
    "nav_pause": 0x22,
    "pause": 0x22,
    "nav_resume": 0x23,
    "resume": 0x23,
    "nav_clear": 0x24,
    "clear": 0x24,
    "status": 0x30,
    "auth": 0x31,
    "heartbeat": 0x3F,
    "gimbal_rotate": 0x40,
    "gimbal_reset": 0x41,
}


HELP_TEXT = """
Commands:
  status
  auth
  takeoff
  land
  gohome | home
  hover
  plansto <lat> <lon> <alt> [speed]
  changesto <lat> <lon> <alt> [speed]
  ascend <n> [speed]
  start | stop | resume | clear
  gimbal_rotate <pitch> <roll> <yaw> [time]
  gimbal_reset
  reconnect
  help
  quit | exit

Examples:
  status
  hover
  plansto 22.542812 113.958902 20 2
  ascend 10 1.5
  gimbal_rotate 0 0 30 1
"""


def calculate_checksum(data: bytes) -> int:
    return sum(data) & 0xFFFF


def build_packet(command: str, lat: float, lon: float, alt: float, speed: float) -> bytes:
    cmd = COMMANDS[command]
    body = struct.pack("<HBBddff", PROTOCOL_HEADER, cmd, 0, lat, lon, alt, speed)
    checksum = calculate_checksum(body)
    return struct.pack(PACKET_FORMAT, PROTOCOL_HEADER, cmd, 0, lat, lon, alt, speed, checksum)


def parse_command_values(parts: List[str]) -> Optional[Tuple[str, float, float, float, float]]:
    if not parts:
        return None

    command = parts[0].lower()
    if command not in COMMANDS:
        raise ValueError(f"unknown command: {command}")

    lat = 0.0
    lon = 0.0
    alt = 0.0
    speed = 0.0

    if command in ("plansto", "changesto"):
        if len(parts) < 4:
            raise ValueError(f"{command} usage: {command} <lat> <lon> <alt> [speed]")
        lat = float(parts[1])
        lon = float(parts[2])
        alt = float(parts[3])
        speed = float(parts[4]) if len(parts) > 4 else 5.0
    elif command == "ascend":
        if len(parts) < 2:
            raise ValueError("ascend usage: ascend <delta_height> [speed]")
        alt = float(parts[1])
        speed = float(parts[2]) if len(parts) > 2 else 0.0
    elif command == "gimbal_rotate":
        if len(parts) < 4:
            raise ValueError("gimbal_rotate usage: gimbal_rotate <pitch> <roll> <yaw> [time]")
        lat = float(parts[1])
        lon = float(parts[2])
        alt = float(parts[3])
        speed = float(parts[4]) if len(parts) > 4 else 0.5

    return command, lat, lon, alt, speed


class TcpInteractiveClient:
    def __init__(self, host: str, port: int, timeout: float) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: Optional[socket.socket] = None

    def connect(self) -> None:
        self.close()
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(self.timeout)
        sock.connect((self.host, self.port))
        self.sock = sock
        print(f"[OK] connected to {self.host}:{self.port}")

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def send(self, command: str, lat: float, lon: float, alt: float, speed: float) -> None:
        if self.sock is None:
            self.connect()

        packet = build_packet(command, lat, lon, alt, speed)
        assert self.sock is not None
        try:
            self.sock.sendall(packet)
        except OSError:
            print("[WARN] send failed, reconnecting once...")
            self.connect()
            assert self.sock is not None
            self.sock.sendall(packet)

        print(f"[TCP] sent {command} lat={lat} lon={lon} alt={alt} speed={speed}")


def run_interactive(client: TcpInteractiveClient) -> int:
    print(HELP_TEXT.strip())
    try:
        client.connect()
    except OSError as exc:
        print(f"[WARN] initial connect failed: {exc}")
        print("[INFO] you can type 'reconnect' after checking network/server.")

    while True:
        try:
            line = input("tcp> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not line:
            continue

        try:
            parts = shlex.split(line)
        except ValueError as exc:
            print(f"[ERROR] parse failed: {exc}")
            continue

        command = parts[0].lower()
        if command in ("quit", "exit", "q"):
            break
        if command in ("help", "h", "?"):
            print(HELP_TEXT.strip())
            continue
        if command == "reconnect":
            try:
                client.connect()
            except OSError as exc:
                print(f"[ERROR] reconnect failed: {exc}")
            continue

        try:
            values = parse_command_values(parts)
            if values is None:
                continue
            client.send(*values)
        except (OSError, ValueError) as exc:
            print(f"[ERROR] {exc}")

    client.close()
    print("[INFO] disconnected")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send TCP control commands.")
    parser.add_argument(
        "command",
        nargs="?",
        choices=sorted(COMMANDS.keys()),
        help="command name. Omit it to enter interactive mode.",
    )
    parser.add_argument("values", nargs="*", help="positional command values")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"default: {DEFAULT_HOST}")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"default: {DEFAULT_PORT}")
    parser.add_argument("--lat", type=float, default=0.0, help="latitude for plansto/changesto")
    parser.add_argument("--lon", type=float, default=0.0, help="longitude for plansto/changesto")
    parser.add_argument("--alt", type=float, default=0.0, help="altitude, or gimbal yaw for gimbal_rotate")
    parser.add_argument("--speed", type=float, default=0.0, help="speed, or rotate time for gimbal_rotate")
    parser.add_argument("--timeout", type=float, default=5.0, help="connect timeout seconds")
    parser.add_argument("--pitch", type=float, default=None, help="gimbal_rotate pitch")
    parser.add_argument("--roll", type=float, default=None, help="gimbal_rotate roll")
    parser.add_argument("--yaw", type=float, default=None, help="gimbal_rotate yaw")
    parser.add_argument("--time", type=float, default=None, help="gimbal_rotate time")
    parser.add_argument("-i", "--interactive", action="store_true", help="force interactive mode")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    client = TcpInteractiveClient(args.host, args.port, args.timeout)

    if args.interactive or args.command is None:
        return run_interactive(client)

    if args.values:
        try:
            args.command, lat, lon, alt, speed = parse_command_values(
                [args.command] + args.values)
        except ValueError as exc:
            print(f"[ERROR] {exc}", file=sys.stderr)
            return 1
    else:
        lat = args.lat
        lon = args.lon
        alt = args.alt
        speed = args.speed

        if args.command == "gimbal_rotate":
            lat = args.pitch if args.pitch is not None else args.lat
            lon = args.roll if args.roll is not None else args.lon
            alt = args.yaw if args.yaw is not None else args.alt
            speed = args.time if args.time is not None else args.speed

    try:
        client.connect()
        client.send(args.command, lat, lon, alt, speed)
    except OSError as exc:
        print(f"[ERROR] TCP send failed: {exc}", file=sys.stderr)
        return 1
    finally:
        client.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
