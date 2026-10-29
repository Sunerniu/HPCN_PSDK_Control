#!/usr/bin/env bash
set -euo pipefail

if [ "${PSDK_USB_BULK_REQUIRED:-0}" != "1" ]; then
  exit 0
fi

wait_seconds="${PSDK_USB_BULK_WAIT_SECONDS:-30}"
case "${wait_seconds}" in
  ''|*[!0-9]*)
    echo "Invalid PSDK_USB_BULK_WAIT_SECONDS: ${wait_seconds}" >&2
    exit 1
    ;;
esac

bulk1_out="${DJI_USB_BULK1_EP_OUT_PATH:-/dev/usb-ffs/bulk1/ep1}"
bulk1_in="${DJI_USB_BULK1_EP_IN_PATH:-/dev/usb-ffs/bulk1/ep2}"
bulk2_out="${DJI_USB_BULK2_EP_OUT_PATH:-/dev/usb-ffs/bulk2/ep1}"
bulk2_in="${DJI_USB_BULK2_EP_IN_PATH:-/dev/usb-ffs/bulk2/ep2}"

elapsed=0
while [ "${elapsed}" -lt "${wait_seconds}" ]; do
  if [ -e "${bulk1_out}" ] && [ -e "${bulk1_in}" ] && \
     [ -e "${bulk2_out}" ] && [ -e "${bulk2_in}" ]; then
    echo "PSDK USB Bulk endpoints are ready."
    exit 0
  fi
  sleep 1
  elapsed=$((elapsed + 1))
done

echo "PSDK USB Bulk endpoints were not ready after ${wait_seconds}s." >&2
echo "Expected: ${bulk1_out} ${bulk1_in} ${bulk2_out} ${bulk2_in}" >&2
echo "Check startup_bulk, FunctionFS mounts, USB gadget descriptors, and UDC state." >&2
exit 1
