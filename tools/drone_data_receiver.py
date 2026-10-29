#!/usr/bin/env python3
"""Receive JSON telemetry packets sent by the AGX."""

import argparse
import json
import socket


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=14550)
    parser.add_argument("--raw", action="store_true")
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((args.bind, args.port))
        print(f"Listening for AGX telemetry on {args.bind}:{args.port}")
        while True:
            payload, source = sock.recvfrom(65535)
            text = payload.decode("utf-8", errors="replace")
            if args.raw:
                print(text)
                continue
            try:
                print(json.dumps(json.loads(text), ensure_ascii=False, indent=2))
            except json.JSONDecodeError:
                print(f"[{source[0]}:{source[1]}] {text}")


if __name__ == "__main__":
    main()
