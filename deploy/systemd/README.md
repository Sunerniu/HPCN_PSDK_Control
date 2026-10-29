# HPCN PSDK systemd service

## Install

Build the AGX executable first:

```bash
cd ~/PSDK/HPCN_PSDK_Control-0.2.0
cmake -S . -B build-usb-bulk \
  -DPSDK_ENABLE_USB_BULK=ON \
  -DPSDK_ENABLE_NETWORK=OFF
cmake --build build-usb-bulk -j"$(nproc)"
```

Check the fixed UDP receiver IP in:

```text
deploy/systemd/hpcn-psdk.env
```

Install and immediately start the service:

```bash
cd ~/PSDK/HPCN_PSDK_Control-0.2.0
sudo ./deploy/systemd/install.sh \
  build-usb-bulk/bin/dji_sdk_demo_on_jetson_cxx
```

The installer:

- verifies `/dev/ttyTHS0` and `/dev/gpiochip0`;
- waits 20 seconds before each start so the aircraft/E-Port UART link can
  finish initializing;
- installs the executable under `/opt/hpcn-psdk`;
- installs a pre-start check for the `bulk1` and `bulk2` FunctionFS endpoints;
- runs the service as root for UART/GPIO access and PPS real-time priority;
- maps PSDK UART0 to `/dev/ttyTHS0` for the direct E-Port-to-Jetson
  TTL connection and disables UART1;
- uses `/var/lib/hpcn-psdk` as the writable working directory;
- stores the DJI rotating log files under `/var/lib/hpcn-psdk/Logs`;
- writes console output to the systemd journal;
- enables automatic restart after an unexpected failure.

## Operate

The shortest commands are:

```bash
# Set the computer IP that receives UDP status packets on port 14550
sudo hpcn-psdkctl set-ip 10.29.213.243

# Enable/disable precise positioning and GPS_precise_time
sudo hpcn-psdkctl precise on
sudo hpcn-psdkctl precise off

# Show all current hardware and feature settings
sudo hpcn-psdkctl show

# Service status and live logs
sudo hpcn-psdkctl status
sudo hpcn-psdkctl logs
```

`set-ip` and `precise on/off` automatically restart the service so the new
configuration takes effect immediately. The UDP destination port remains
fixed at `14550`.

Service control shortcuts:

```bash
sudo hpcn-psdkctl start
sudo hpcn-psdkctl stop
sudo hpcn-psdkctl restart
sudo hpcn-psdkctl enable
sudo hpcn-psdkctl disable
```

Equivalent native systemd commands:

```bash
# Current status
sudo systemctl status hpcn-psdk --no-pager

# Follow live output
sudo journalctl -u hpcn-psdk -f

# Logs from the current boot
sudo journalctl -u hpcn-psdk -b --no-pager

# Start, stop, or restart now
sudo systemctl start hpcn-psdk
sudo systemctl stop hpcn-psdk
sudo systemctl restart hpcn-psdk

# Enable or disable startup at boot
sudo systemctl enable hpcn-psdk
sudo systemctl disable hpcn-psdk

# Disable startup and stop it immediately
sudo systemctl disable --now hpcn-psdk

# Enable startup and start it immediately
sudo systemctl enable --now hpcn-psdk
```

## Change configuration

Edit:

```bash
sudo nano /etc/hpcn-psdk/hpcn-psdk.env
```

Then apply it:

```bash
sudo systemctl restart hpcn-psdk
```

The service target IP is fixed at startup by `UDP_TARGET_IP`; incoming UDP
commands and heartbeat packets do not change it.

For the tested M300 Extension Port setup in China,
`AIRCRAFT_TIME_UTC_OFFSET_MINUTES=480` converts the aircraft UTC+8 wall time
to a standard Unix UTC timestamp.

## Verify hardware ownership

```bash
sudo fuser -v /dev/ttyTHS0
sudo gpioinfo gpiochip0 | sed -n '86p'
```

Only one PSDK process may own `/dev/ttyTHS0`. If another manually started PSDK
process owns the port, stop that process before starting this service.

For direct E-Port TX/RX wiring, keep:

```text
DJI_UART0_DEVICE=/dev/ttyTHS0
DJI_UART1_ENABLE=0
DJI_UART1_DEVICE=/dev/ttyACM0
```

PSDK UART1 is reserved for the M300/M350 Extension Port USB virtual serial
channel. It must never be mapped to the same `/dev/ttyTHS0` device as UART0.
