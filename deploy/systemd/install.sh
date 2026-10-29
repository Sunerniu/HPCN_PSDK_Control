#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="hpcn-psdk.service"
INSTALL_DIR="/opt/hpcn-psdk"
CONFIG_DIR="/etc/hpcn-psdk"
STATE_DIR="/var/lib/hpcn-psdk"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DEFAULT_BINARY="${PROJECT_DIR}/build/bin/dji_sdk_demo_on_jetson_cxx"
BINARY_PATH="${1:-${DEFAULT_BINARY}}"

if [ "$(id -u)" -ne 0 ]; then
  echo "Please run with sudo: sudo $0 [binary_path]" >&2
  exit 1
fi

if [ ! -x "${BINARY_PATH}" ]; then
  echo "Executable not found: ${BINARY_PATH}" >&2
  echo "Build the project first, or pass the executable path as argument." >&2
  exit 1
fi

if [ ! -c /dev/ttyTHS0 ]; then
  echo "UART device missing: /dev/ttyTHS0" >&2
  exit 1
fi

if [ ! -c /dev/gpiochip0 ]; then
  echo "GPIO device missing: /dev/gpiochip0" >&2
  exit 1
fi

install -d -m 0755 "${INSTALL_DIR}" "${CONFIG_DIR}" \
  "${STATE_DIR}" "${STATE_DIR}/Logs"
install -m 0755 "${BINARY_PATH}" \
  "${INSTALL_DIR}/dji_sdk_demo_on_jetson_cxx"
install -m 0755 "${SCRIPT_DIR}/wait-usb-bulk.sh" \
  "${INSTALL_DIR}/wait-usb-bulk.sh"
install -m 0644 "${SCRIPT_DIR}/${SERVICE_NAME}" \
  "/etc/systemd/system/${SERVICE_NAME}"
install -m 0755 "${SCRIPT_DIR}/hpcn-psdkctl" \
  "/usr/local/sbin/hpcn-psdkctl"

if [ ! -f "${CONFIG_DIR}/hpcn-psdk.env" ]; then
  install -m 0644 "${SCRIPT_DIR}/hpcn-psdk.env" \
    "${CONFIG_DIR}/hpcn-psdk.env"
  echo "Installed initial configuration: ${CONFIG_DIR}/hpcn-psdk.env"
else
  echo "Preserved existing configuration: ${CONFIG_DIR}/hpcn-psdk.env"
fi

if ! grep -q '^DJI_UART1_ENABLE=' "${CONFIG_DIR}/hpcn-psdk.env"; then
  printf '\nDJI_UART1_ENABLE=0\n' \
    >>"${CONFIG_DIR}/hpcn-psdk.env"
  echo "Disabled USB virtual UART1 for direct E-Port TTL wiring."
fi

if grep -q '^DJI_UART1_DEVICE=/dev/ttyTHS0$' "${CONFIG_DIR}/hpcn-psdk.env"; then
  sed -i 's|^DJI_UART1_DEVICE=/dev/ttyTHS0$|DJI_UART1_DEVICE=/dev/ttyACM0|' \
    "${CONFIG_DIR}/hpcn-psdk.env"
  echo "Removed invalid UART1 sharing of /dev/ttyTHS0."
fi

append_config_if_missing() {
  local key="$1"
  local value="$2"
  if ! grep -q "^${key}=" "${CONFIG_DIR}/hpcn-psdk.env"; then
    printf '%s=%s\n' "${key}" "${value}" >>"${CONFIG_DIR}/hpcn-psdk.env"
  fi
}

# Preserve existing deployments while making the new Liveview settings
# available after reinstall. Liveview stays disabled until explicitly enabled.
append_config_if_missing LIVEVIEW_ENABLE 0
append_config_if_missing LIVEVIEW_CAMERA fpv
append_config_if_missing LIVEVIEW_SOURCE default
append_config_if_missing LIVEVIEW_OUTPUT_FILE ""
if grep -qE '^LIVEVIEW_(UDP_(HOST|PORT)|RTSP_(PORT|PATH)|IFRAME_INTERVAL_SECONDS)=' \
    "${CONFIG_DIR}/hpcn-psdk.env"; then
  sed -i '/^LIVEVIEW_UDP_HOST=/d; /^LIVEVIEW_UDP_PORT=/d; /^LIVEVIEW_RTSP_PORT=/d; /^LIVEVIEW_RTSP_PATH=/d; /^LIVEVIEW_IFRAME_INTERVAL_SECONDS=/d' \
    "${CONFIG_DIR}/hpcn-psdk.env"
  echo "Removed obsolete Liveview DLV1/UDP and legacy RTSP settings."
fi
append_config_if_missing LIVEVIEW_QUEUE_MB 8
append_config_if_missing RTSP_ENABLE 1
append_config_if_missing RTSP_PORT 8554
append_config_if_missing RTSP_PATH /m350/fpv
append_config_if_missing RTSP_ADVERTISED_HOST ""
append_config_if_missing VIDEO_WIDTH 640
append_config_if_missing VIDEO_HEIGHT 360
append_config_if_missing VIDEO_FPS 10
append_config_if_missing VIDEO_BITRATE_KBPS 800
append_config_if_missing VIDEO_IDR_INTERVAL_SECONDS 2
append_config_if_missing PSDK_USB_BULK_REQUIRED 0
append_config_if_missing PSDK_USB_BULK_WAIT_SECONDS 30
append_config_if_missing DJI_USB_BULK_VID 0x0955
append_config_if_missing DJI_USB_BULK_PID 0x7020
append_config_if_missing DJI_USB_BULK1_INTERFACE 7
append_config_if_missing DJI_USB_BULK1_EP_IN 0x88
append_config_if_missing DJI_USB_BULK1_EP_OUT 0x05
append_config_if_missing DJI_USB_BULK1_EP_OUT_PATH /dev/usb-ffs/bulk1/ep1
append_config_if_missing DJI_USB_BULK1_EP_IN_PATH /dev/usb-ffs/bulk1/ep2
append_config_if_missing DJI_USB_BULK2_INTERFACE 8
append_config_if_missing DJI_USB_BULK2_EP_IN 0x89
append_config_if_missing DJI_USB_BULK2_EP_OUT 0x06
append_config_if_missing DJI_USB_BULK2_EP_OUT_PATH /dev/usb-ffs/bulk2/ep1
append_config_if_missing DJI_USB_BULK2_EP_IN_PATH /dev/usb-ffs/bulk2/ep2

systemctl daemon-reload
systemctl enable "${SERVICE_NAME}"
systemctl restart "${SERVICE_NAME}"

echo
echo "Service installed and started."
echo "Status: sudo systemctl status ${SERVICE_NAME} --no-pager"
echo "Logs:   sudo journalctl -u ${SERVICE_NAME} -f"
echo "Config: sudo hpcn-psdkctl show"
