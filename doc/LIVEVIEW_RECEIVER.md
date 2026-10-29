# M350 E-Port USB Bulk low-bandwidth RTSP Liveview

The application keeps the existing PSDK UART command channel and E-Port USB
Bulk Liveview receiver, but does not forward the M350 source bitrate directly.
On Jetson AGX Orin it uses NVIDIA hardware to decode, resize, and encode a
low-bandwidth H.264 rendition:

```text
M350 FPV H.264 -> E-Port USB Bulk -> DjiLiveview callback -> appsrc
  -> h264parse -> nvv4l2decoder -> nvvidconv (640x360)
  -> videorate (drop to 10 fps) -> nvv4l2h264enc (two-pass CBR 800 kbps)
  -> h264parse -> rtph264pay -> RTSP
```

Default URL: `rtsp://<AGX_IP>:8554/m350/fpv`.

`videorate` only drops timestamped NVMM buffers after hardware scaling. H.264
decode, scaling, and H.264 encode use Jetson plugins. The application checks
for `nvv4l2decoder`, `nvvidconv`, and `nvv4l2h264enc` at startup and does not
silently fall back to CPU software codecs.

## 1. USB Bulk prerequisite

Keep `/dev/ttyTHS0` for the PSDK command channel. Connect M350 E-Port USB to an
AGX device/OTG-capable USB-C port. The M350 is the USB host and AGX is the USB
device. Before starting the application, verify:

```bash
ps -ef | grep '[s]tartup_bulk'
ls -l /dev/usb-ffs/bulk1/ep1 /dev/usb-ffs/bulk1/ep2
ls -l /dev/usb-ffs/bulk2/ep1 /dev/usb-ffs/bulk2/ep2
cat /sys/class/usb_role/usb2-0-role-switch/role
cat /sys/class/udc/*/state
```

The role must be `device`; with the powered aircraft connected, UDC should be
`configured`. USB Bulk does not need `usb0` or `DJI_NETWORK_DEVICE`. The RTSP
client reaches AGX over its normal Ethernet/Wi-Fi interface.

The FunctionFS interface/endpoint settings must match the active
`startup_bulk` descriptors. This deployment defaults to:

```text
DJI_USB_BULK1_INTERFACE=7
DJI_USB_BULK1_EP_IN=0x88
DJI_USB_BULK1_EP_OUT=0x05
DJI_USB_BULK2_INTERFACE=8
DJI_USB_BULK2_EP_IN=0x89
DJI_USB_BULK2_EP_OUT=0x06
```

## 2. AGX dependencies

Install the standard GStreamer headers, RTSP server library, parser, payloader,
and tools:

```bash
sudo apt update
sudo apt install -y \
  pkg-config \
  libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  libgstrtspserver-1.0-dev \
  gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad \
  gstreamer1.0-tools \
  ffmpeg
```

The NVIDIA elements are supplied by JetPack/L4T. Confirm all required elements
before compiling or starting the service:

```bash
gst-inspect-1.0 appsrc h264parse videorate rtph264pay
gst-inspect-1.0 nvv4l2decoder nvvidconv nvv4l2h264enc
pkg-config --modversion gstreamer-rtsp-server-1.0
```

If an NVIDIA element is missing, repair the matching JetPack/L4T multimedia
installation. Do not install a desktop software codec as a fallback.

## 3. Compile

Use a clean USB Bulk build:

```bash
cd ~/Desktop/zzy/Live_version/HPCN_PSDK_Control-0.2.0
mkdir -p build
cd build
cmake .. -DPSDK_ENABLE_USB_BULK=ON
make -j"$(nproc)"
```

If this build directory previously configured another version, remove only its
CMake artifacts or create a new directory such as `build-rtsp-lowbandwidth`.
Do not delete the retained old source tree.

## 4. Runtime configuration

For a manual test, run from the build directory:

```bash
sudo env \
  DJI_UART0_DEVICE=/dev/ttyTHS0 \
  DJI_UART1_ENABLE=0 \
  ENABLE_PRECISE_POSITIONING=0 \
  LIVEVIEW_ENABLE=1 \
  LIVEVIEW_CAMERA=fpv \
  LIVEVIEW_SOURCE=default \
  LIVEVIEW_QUEUE_MB=8 \
  LIVEVIEW_OUTPUT_FILE= \
  RTSP_ENABLE=1 \
  RTSP_PORT=8554 \
  RTSP_PATH=/m350/fpv \
  RTSP_ADVERTISED_HOST=10.29.174.183 \
  VIDEO_WIDTH=640 \
  VIDEO_HEIGHT=360 \
  VIDEO_FPS=10 \
  VIDEO_BITRATE_KBPS=800 \
  VIDEO_IDR_INTERVAL_SECONDS=2 \
  ./bin/dji_sdk_demo_on_jetson_cxx
```

`RTSP_ADVERTISED_HOST` only controls the URL printed in the log. Leave it empty
to auto-detect an address; the server always listens on `0.0.0.0`.

For systemd, build first and then install:

```bash
cd ~/Desktop/zzy/Live_version/HPCN_PSDK_Control-0.2.0
sudo ./deploy/systemd/install.sh
sudoedit /etc/hpcn-psdk/hpcn-psdk.env
sudo systemctl restart hpcn-psdk
sudo journalctl -u hpcn-psdk -f
```

Set `PSDK_USB_BULK_REQUIRED=1` in the installed environment file. The installer
removes old video-only DLV1/UDP and legacy `LIVEVIEW_RTSP_*` keys but preserves
the unrelated MQTT/telemetry and `UDP_TARGET_IP` settings.

Expected startup messages include:

```text
RTSP hardware transcoder: 640x360 10 fps 800 kbps IDR=2 s
RTSP server listening at rtsp://0.0.0.0:8554/m350/fpv
RTSP URL: rtsp://10.29.174.183:8554/m350/fpv video=640x360@10 fps bitrate=800 kbps IDR=2 s
```

## 5. Playback and acceptance checks

Play from the remote computer using RTSP interleaved TCP:

```bash
ffplay -rtsp_transport tcp rtsp://10.29.174.183:8554/m350/fpv
```

Check codec, resolution, and advertised frame rate:

```bash
ffprobe -v error -rtsp_transport tcp \
  -select_streams v:0 \
  -show_entries stream=codec_name,width,height,r_frame_rate,avg_frame_rate \
  -of default=noprint_wrappers=1 \
  rtsp://10.29.174.183:8554/m350/fpv
```

Expected values are H.264, `640`, `360`, and approximately `10/1`. Measure a
60-second transport-stream sample without transcoding:

```bash
ffmpeg -y -rtsp_transport tcp \
  -i rtsp://10.29.174.183:8554/m350/fpv \
  -t 60 -map 0:v:0 -c copy -f mpegts /tmp/m350-rtsp-test.ts

ffprobe -v error -show_entries format=duration,size \
  -of default=noprint_wrappers=1 /tmp/m350-rtsp-test.ts
```

Calculate average transport bitrate as `size * 8 / duration`. MPEG-TS adds its
own overhead, so it will read slightly above the 800 kbps elementary-video
target. For actual interface use, run `sudo iftop -i <interface>` or
`sar -n DEV 1` while MQTT and flight control are also active.

Verify hardware engines and CPU utilization on AGX while a client is playing:

```bash
sudo tegrastats
ps -C dji_sdk_demo_on_jetson_cxx -o pid,%cpu,%mem,cmd
```

The 800 kbps video target plus RTP/TCP/IP overhead normally leaves several
hundred kbps inside a 1.2 Mbps budget for RTSP control, MQTT telemetry, and
short-term variation. CBR is rate control, not an absolute per-millisecond wire
cap, so acceptance must use a sufficiently long interface measurement.

## 6. Recovery and troubleshooting

- The NVIDIA encoder emits an IDR every `10 fps * 2 s = 20` output frames.
- Two-pass CBR and a small `vbv-size` reduce bitrate excursions around IDR
  frames; the installed AGX encoder exposes and supports both properties.
- `insert-sps-pps=true` and `rtph264pay config-interval=-1` repeat SPS/PPS at
  every output IDR, allowing a late client to begin within about two seconds.
- The worker requests a fresh PSDK source I-frame when a media pipeline is
  created, after queue overflow, and every two seconds. This lets the hardware
  decoder recover after an input interruption or lost reference frame.
- Queues are bounded and downstream-leaky so a slow client does not create
  unbounded latency or memory growth.
- On shutdown, PSDK streaming stops first, then the worker, Liveview module,
  RTSP main loop, appsrc/media references, and optional recording are released.

If RTSP returns 503 or no picture appears, run with `GST_DEBUG=2`, inspect the
service journal, and confirm the exact encoder property names on the installed
JetPack release:

```bash
gst-inspect-1.0 nvv4l2h264enc | grep -E \
  'bitrate|control-rate|iframeinterval|idrinterval|insert-sps-pps|maxperf'
ss -lntp | grep 8554
```
