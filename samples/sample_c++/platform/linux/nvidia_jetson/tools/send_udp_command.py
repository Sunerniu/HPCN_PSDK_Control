#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
UDP command sender for HPCN PSDK Control.

Default target: 10.129.155.64:8080
Packet format: <HBBddffH>, 30 bytes, same as protocol.h.
"""

import argparse
import json
import socket
import struct
import sys
from typing import List, Tuple


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


def calculate_checksum(data: bytes) -> int:
    return sum(data) & 0xFFFF


def build_packet(command: str, lat: float, lon: float, alt: float, speed: float) -> bytes:
    cmd = COMMANDS[command]
    body = struct.pack("<HBBddff", PROTOCOL_HEADER, cmd, 0, lat, lon, alt, speed)
    checksum = calculate_checksum(body)
    return struct.pack(PACKET_FORMAT, PROTOCOL_HEADER, cmd, 0, lat, lon, alt, speed, checksum)


def parse_command_values(command: str, values: List[str], args: argparse.Namespace) -> Tuple[float, float, float, float]:
    lat = args.lat
    lon = args.lon
    alt = args.alt
    speed = args.speed

    if values:
        if command in ("plansto", "changesto"):
            if len(values) < 3:
                raise ValueError(f"{command} usage: {command} <lat> <lon> <alt> [speed]")
            lat = float(values[0])
            lon = float(values[1])
            alt = float(values[2])
            speed = float(values[3]) if len(values) > 3 else 5.0
        elif command == "ascend":
            if len(values) < 1:
                raise ValueError("ascend usage: ascend <delta_height> [speed]")
            alt = float(values[0])
            speed = float(values[1]) if len(values) > 1 else 0.0
        elif command == "gimbal_rotate":
            if len(values) < 3:
                raise ValueError("gimbal_rotate usage: gimbal_rotate <pitch> <roll> <yaw> [time]")
            lat = float(values[0])
            lon = float(values[1])
            alt = float(values[2])
            speed = float(values[3]) if len(values) > 3 else 0.5
    elif command == "gimbal_rotate":
        lat = args.pitch if args.pitch is not None else args.lat
        lon = args.roll if args.roll is not None else args.lon
        alt = args.yaw if args.yaw is not None else args.alt
        speed = args.time if args.time is not None else args.speed

    return lat, lon, alt, speed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send one UDP control command.")
    parser.add_argument("command", choices=sorted(COMMANDS.keys()), help="command name")
    parser.add_argument("values", nargs="*", help="positional command values")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"default: {DEFAULT_HOST}")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"default: {DEFAULT_PORT}")
    parser.add_argument("--lat", type=float, default=0.0, help="latitude for plansto/changesto")
    parser.add_argument("--lon", type=float, default=0.0, help="longitude for plansto/changesto")
    parser.add_argument("--alt", type=float, default=0.0, help="altitude, ascend delta, or gimbal yaw")
    parser.add_argument("--speed", type=float, default=0.0, help="speed, or rotate time for gimbal_rotate")
    parser.add_argument("--pitch", type=float, default=None, help="gimbal_rotate pitch")
    parser.add_argument("--roll", type=float, default=None, help="gimbal_rotate roll")
    parser.add_argument("--yaw", type=float, default=None, help="gimbal_rotate yaw")
    parser.add_argument("--time", type=float, default=None, help="gimbal_rotate time")
    parser.add_argument("--json-heartbeat", action="store_true",
                        help="send JSON heartbeat instead of binary heartbeat packet")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        if args.json_heartbeat:
            payload = json.dumps({"heartbeat": True}, separators=(",", ":")).encode("utf-8")
            desc = "json-heartbeat"
        else:
            lat, lon, alt, speed = parse_command_values(args.command, args.values, args)
            payload = build_packet(args.command, lat, lon, alt, speed)
            desc = f"{args.command} lat={lat} lon={lon} alt={alt} speed={speed}"
    except ValueError as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 1

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sent = sock.sendto(payload, (args.host, args.port))
    except OSError as exc:
        print(f"[ERROR] UDP send failed: {exc}", file=sys.stderr)
        return 1

    print(f"[UDP] sent {sent} bytes to {args.host}:{args.port}: {desc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

