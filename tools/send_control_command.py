#!/usr/bin/env python3
"""Send a 30-byte HPCN flight-control command over UDP or TCP."""

import argparse
import socket
import struct


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
    "gimbal_reset": 0x41,
}


def build_packet(args: argparse.Namespace) -> bytes:
    first_28 = struct.pack(
        "<HBBddff",
        HEADER,
        COMMANDS[args.command],
        0,
        args.latitude,
        args.longitude,
        args.altitude,
        args.speed,
    )
    return first_28 + struct.pack("<H", sum(first_28) & 0xFFFF)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, help="AGX IP address")
    parser.add_argument("--transport", choices=("udp", "tcp"), default="udp")
    parser.add_argument("--port", type=int, help="default: UDP 14551, TCP 8080")
    parser.add_argument("command", choices=sorted(COMMANDS))
    parser.add_argument("--latitude", type=float, default=0.0)
    parser.add_argument("--longitude", type=float, default=0.0)
    parser.add_argument("--altitude", type=float, default=0.0)
    parser.add_argument("--speed", type=float, default=0.0)
    args = parser.parse_args()

    port = args.port or (14551 if args.transport == "udp" else 8080)
    packet = build_packet(args)

    if args.transport == "udp":
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.sendto(packet, (args.host, port))
    else:
        with socket.create_connection((args.host, port), timeout=5) as sock:
            sock.sendall(packet)

    print(f"Sent {args.command} to {args.host}:{port} via {args.transport.upper()}")


if __name__ == "__main__":
    main()
