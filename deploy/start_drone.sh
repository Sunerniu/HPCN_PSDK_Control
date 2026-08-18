#!/bin/bash
set -e

APP_DIR="/home/hpcn/Desktop/PSDK/psdk-3.14.1-fc30/build/bin"
APP_NAME="dji_sdk_demo_on_jetson_cxx"
UART_DEV="${DJI_UART0_DEVICE:-/dev/ttyTHS0}"
PPS_GPIOCHIP="${PPS_GPIOCHIP:-/dev/gpiochip0}"
PPS_GPIO_LINE="${PPS_GPIO_LINE:-123}"
UDP_TARGET_IP="${UDP_TARGET_IP:-192.168.0.1}"

export PPS_GPIOCHIP
export PPS_GPIO_LINE
export UDP_TARGET_IP
export DJI_UART0_DEVICE="$UART_DEV"

echo "[INFO] Waiting for UART device: $UART_DEV"
echo "[INFO] PPS GPIO config: $PPS_GPIOCHIP line $PPS_GPIO_LINE"
echo "[INFO] UDP status target: $UDP_TARGET_IP"

for i in $(seq 1 30); do
    if [ -e "$UART_DEV" ]; then
        echo "[INFO] UART device found: $UART_DEV"
        chmod 666 "$UART_DEV" 2>/dev/null || true
        break
    fi

    echo "[WARN] UART device not found, retry $i/30"
    sleep 1
done

if [ ! -e "$UART_DEV" ]; then
    echo "[ERROR] UART device $UART_DEV not found, exit."
    exit 1
fi

mkdir -p "$APP_DIR/Logs"
chown -R hpcn:hpcn "$APP_DIR/Logs" 2>/dev/null || true

cd "$APP_DIR"
echo "[INFO] Starting $APP_NAME ..."
exec "./$APP_NAME"
