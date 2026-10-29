# M350 PSDK + AGX + PC 控制台项目交接文档

> 交接日期：2026-10-29
>
> 当前最新代码目录：`/Users/ergou/Code/HPCN_PSDK_Control-0.2.1`
>
> 目标平台：DJI M350 + E-Port + Jetson AGX Orin + PC 控制端

## 1. 先说结论：最新代码在哪里

当前唯一完整、应继续维护的版本是：

```text
/Users/ergou/Code/HPCN_PSDK_Control-0.2.1
```

其中：

- AGX 端 PSDK、USB Bulk、飞行控制、遥测和 RTSP 代码位于项目根目录及 `samples/sample_c++/platform/linux/nvidia_jetson/`。
- PC 端 AEROLINK 控制台位于 `pc-console/`。
- `HPCN_PSDK_Control-0.2.0` 当前不是完整源码，只剩预览图和本交接文档，不应继续在该目录开发或编译。
- `HPCN_PSDK_Control-fc30-psdk-3.14.1` 是另一份独立代码，不属于本次已集成的 0.2.1 版本。

重要：`0.2.1` 当前没有 `.git`，因此没有可追溯的提交历史、分支或标签。接手后第一件事应建立 Git 仓库并提交一个交接基线。

Git `latest` 分支不包含真实 DJI App ID、Key、License 或开发者账号。首次编译前，将
`dji_sdk_app_info.local.h.example` 复制为 `dji_sdk_app_info.local.h`，然后仅在本机填写真实值；本地文件已被 `.gitignore` 排除。

## 2. 系统组成

```text
M350 FPV 相机
    │ H.264 / E-Port USB Bulk
    ▼
Jetson AGX Orin
    ├─ PSDK UART0：/dev/ttyTHS0
    ├─ FunctionFS：bulk1 + bulk2
    ├─ NVIDIA 硬件解码、缩放、编码
    ├─ RTSP：rtsp://<AGX_IP>:8554/m350/fpv
    ├─ TCP 8080：飞行控制命令
    ├─ UDP 14551：控制命令 + JSON 心跳
    └─ UDP 14550 → PC：遥测 JSON
            │
            ▼
PC AEROLINK 控制台
    ├─ Browser UI：http://localhost:3000
    ├─ Local Bridge：http://127.0.0.1:4100
    ├─ RTSP → FFmpeg copy/remux → HLS
    └─ 飞行、航点、高度、云台、遥测和视频界面
```

## 3. 目录和关键文件

| 路径 | 作用 |
|---|---|
| `CMakeLists.txt` | AGX 端顶层 CMake 入口 |
| `psdk_lib/` | DJI PSDK 3.9.2 build 2125 头文件和静态库 |
| `samples/sample_c++/platform/linux/nvidia_jetson/CMakeLists.txt` | AGX 可执行程序构建配置；USB Bulk 与 GStreamer 依赖在此配置 |
| `samples/.../application/main.cpp` | AGX 应用生命周期和各模块初始化/退出顺序 |
| `samples/.../application/liveview_receiver.cpp` | PSDK Liveview 初始化、H.264 回调、缓存、I 帧请求和可选原始码流保存 |
| `samples/.../application/rtsp_liveview_server.cpp` | GStreamer/NVIDIA 硬件转码与 RTSP Server |
| `samples/.../application/protocol.h` | PC→AGX 30 字节控制协议与命令码 |
| `samples/.../application/tcp_server.cpp` | TCP 8080 控制服务 |
| `samples/.../application/udp_receiver.cpp` | UDP 14551 控制和心跳接收 |
| `samples/.../application/udp_sender.cpp` | UDP 14550 遥测发送 |
| `samples/.../application/command_control.cpp` | 起飞、降落、返航、航点、高度等控制逻辑 |
| `samples/.../application/gimbal_control.cpp` | 云台控制 |
| `samples/.../hal/hal_usb_bulk.c` | PSDK USB Bulk HAL |
| `deploy/systemd/` | AGX systemd 服务、环境变量、安装和 USB Bulk 等待脚本 |
| `doc/LIVEVIEW_RECEIVER.md` | USB Bulk、RTSP、编译和验收说明 |
| `doc/CONTROL_COMMAND_PROTOCOL.md` | 完整控制包、命令码、心跳和遥测协议 |
| `pc-console/app/page.tsx` | PC 主界面 |
| `pc-console/bridge/server.mjs` | PC 本地 TCP/UDP/RTSP-HLS 桥接服务 |
| `pc-console/bridge/protocol.mjs` | PC 控制包构造和参数校验 |
| `pc-console/docs/ARCHITECTURE.md` | PC 控制台架构与安全边界 |

`m350-fpv.h264` 是约 89 MB 的测试录像，不是源码。交付源码时可以排除。`pc-console/node_modules`、`.vinext`、`.wrangler`、`.next` 和 `bridge/runtime` 都是依赖、缓存或运行时数据，也不应作为源代码交付。

## 4. 已实现的主要能力

### 4.1 AGX 端

- UART0 通过 `/dev/ttyTHS0` 连接 M350 E-Port PSDK 控制通道。
- USB Bulk 通过 Jetson FunctionFS `bulk1`、`bulk2` 接收 Liveview H.264。
- 默认视频源为 FPV：`LIVEVIEW_CAMERA=fpv`、`LIVEVIEW_SOURCE=default`。
- 调用的 Liveview 核心接口：
  - `DjiLiveview_Init()`
  - `DjiLiveview_StartH264Stream()`
  - `DjiLiveview_RequestIntraframeFrameData()`
  - `DjiLiveview_StopH264Stream()`
  - `DjiLiveview_Deinit()`
- GStreamer 硬件转码链路：

```text
PSDK H.264 callback
→ appsrc
→ h264parse
→ nvv4l2decoder
→ nvvidconv（640×360）
→ videorate（10 fps）
→ nvv4l2h264enc（800 kbps）
→ h264parse
→ rtph264pay
→ RTSP Server
```

- IDR 周期 2 秒，编码器插入 SPS/PPS，并周期性请求 PSDK 源 I 帧。
- 可选将 PSDK 原始 H.264 保存到文件；`LIVEVIEW_OUTPUT_FILE` 为空时不保存。
- TCP/UDP 飞行控制、JSON 心跳、GPS/RTK/姿态/电池/导航状态遥测。
- PPS 时间同步与可选精准定位。
- systemd 开机启动和自动重启。

### 4.2 PC 端

- 深色 AEROLINK 任务控制台。
- FPV 视频播放、姿态仪、位置、高度、速度、电池、RTK/GPS、导航队列展示。
- 起飞、降落、悬停、返航、取消返航、确认/强制降落。
- 航点追加、目标替换、导航暂停/恢复/清空。
- 绝对高度控制、云台俯仰和复位。
- 危险操作二次确认、通信事件日志、AGX 地址与端口设置。
- 浏览器不能直接访问原生 TCP/UDP/RTSP，所以采用“浏览器 UI + 本机 Node.js Bridge”。
- PC 视频链路只做 RTSP→HLS 重新封装，不在 PC 上再次编码。

## 5. 网络接口和端口

| 方向 | 协议/端口 | 数据 | 用途 |
|---|---|---|---|
| PC → AGX | TCP 8080 | 30 字节小端控制包 | 飞行控制，可选持久 TCP |
| PC → AGX | UDP 14551 | 30 字节控制包 | 默认控制通道 |
| PC → AGX | UDP 14551 | JSON | 每秒心跳 |
| AGX → PC | UDP 14550 | JSON | 快/慢遥测包 |
| PC → AGX | RTSP TCP/UDP 8554 | H.264/RTP | FPV 视频 |
| 浏览器 → PC Bridge | HTTP/SSE 4100 | JSON/SSE/HLS | 本地桥接 API |
| 浏览器 | HTTP 3000 | Web UI | AEROLINK 页面 |

默认 RTSP 地址：

```text
rtsp://<AGX_IP>:8554/m350/fpv
```

控制二进制包固定 30 字节、小端序，Python 格式为 `<HBBddffH`，包头为 `0xAA55`。完整命令表见 `doc/CONTROL_COMMAND_PROTOCOL.md`。

注意：当前 AGX 不返回控制命令 ACK。PC 显示“已发送”只表示完成网络写入，必须通过遥测确认动作是否真正执行。

## 6. AGX 构建和部署

### 6.1 依赖

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

NVIDIA 插件来自 JetPack/L4T，启动前必须确认：

```bash
gst-inspect-1.0 appsrc h264parse videorate rtph264pay
gst-inspect-1.0 nvv4l2decoder nvvidconv nvv4l2h264enc
pkg-config --modversion gstreamer-rtsp-server-1.0
```

### 6.2 编译

以下路径以最新版本 `0.2.1` 为准：

```bash
cd ~/Desktop/zzy/Live_version/HPCN_PSDK_Control-0.2.1
cmake -S . -B build-usb-bulk \
  -DPSDK_ENABLE_USB_BULK=ON \
  -DPSDK_ENABLE_NETWORK=OFF
cmake --build build-usb-bulk -j"$(nproc)"
```

输出程序：

```text
build-usb-bulk/bin/dji_sdk_demo_on_jetson_cxx
```

### 6.3 推荐生产配置

编辑 `/etc/hpcn-psdk/hpcn-psdk.env`，至少确认：

```dotenv
DJI_UART0_DEVICE=/dev/ttyTHS0
DJI_UART1_ENABLE=0
UDP_TARGET_IP=<PC实际IP>
ENABLE_PRECISE_POSITIONING=0

LIVEVIEW_ENABLE=1
LIVEVIEW_CAMERA=fpv
LIVEVIEW_SOURCE=default
LIVEVIEW_OUTPUT_FILE=
LIVEVIEW_QUEUE_MB=8

RTSP_ENABLE=1
RTSP_PORT=8554
RTSP_PATH=/m350/fpv
RTSP_ADVERTISED_HOST=
VIDEO_WIDTH=640
VIDEO_HEIGHT=360
VIDEO_FPS=10
VIDEO_BITRATE_KBPS=800
VIDEO_IDR_INTERVAL_SECONDS=2

PSDK_USB_BULK_REQUIRED=1
PSDK_USB_BULK_WAIT_SECONDS=30
```

`RTSP_ADVERTISED_HOST` 只影响日志打印的 URL，不决定监听地址；RTSP Server 始终监听 `0.0.0.0:8554`。通常留空即可自动选择 AGX 地址。

当前源码中的 `deploy/systemd/hpcn-psdk.env` 模板仍是：

```dotenv
LIVEVIEW_ENABLE=0
PSDK_USB_BULK_REQUIRED=0
```

这是为了兼容旧部署的保守默认值，但对于本项目的视频生产部署必须改成 `1`。安装脚本会保留已经存在的 `/etc/hpcn-psdk/hpcn-psdk.env`，重新安装不会自动覆盖旧配置。

### 6.4 安装 systemd 服务

```bash
cd ~/Desktop/zzy/Live_version/HPCN_PSDK_Control-0.2.1
sudo ./deploy/systemd/install.sh \
  build-usb-bulk/bin/dji_sdk_demo_on_jetson_cxx

sudoedit /etc/hpcn-psdk/hpcn-psdk.env
sudo systemctl restart hpcn-psdk
sudo journalctl -u hpcn-psdk -f
```

安装位置：

- 程序：`/opt/hpcn-psdk/dji_sdk_demo_on_jetson_cxx`
- 配置：`/etc/hpcn-psdk/hpcn-psdk.env`
- 工作目录和日志：`/var/lib/hpcn-psdk`
- 服务：`hpcn-psdk.service`
- 管理命令：`hpcn-psdkctl`

## 7. USB Bulk 启动前检查

```bash
ps -ef | grep '[s]tartup_bulk'
ls -l /dev/usb-ffs/bulk1/ep{1,2}
ls -l /dev/usb-ffs/bulk2/ep{1,2}
cat /sys/class/usb_role/usb2-0-role-switch/role
cat /sys/class/udc/3550000.xudc/state
```

正确状态：

```text
role=device
udc=configured
```

状态解释：

- `role=host`：AGX USB-C 当前处于主机角色，M350 无法将其枚举为 USB Device。
- `role=device, udc=default`：AGX 已切为 Device，但 USB Gadget 尚未被 M350 枚举；常见原因是飞机未上电、线材/端口错误、FunctionFS 描述未绑定或物理链路未建立。
- `role=device, udc=configured`：M350 已完成 USB 枚举，可以继续启动 PSDK Liveview。

USB Bulk 不需要 `usb0` 网卡，也不需要 `DJI_NETWORK_DEVICE`。PC 访问 RTSP 使用 AGX 正常的以太网或 Wi-Fi 地址。

## 8. PC 控制台启动

### 8.1 依赖

- Node.js 22.13 或更高版本。
- FFmpeg 可从 `PATH` 直接执行。
- PC 防火墙允许接收 UDP 14550。
- PC 能访问 AGX 的 TCP 8080、UDP 14551、RTSP 8554。

### 8.2 启动

```bash
cd /Users/ergou/Code/HPCN_PSDK_Control-0.2.1/pc-console
npm install
npm run dev
```

浏览器打开：

```text
http://localhost:3000
```

PC Bridge 监听：

```text
http://127.0.0.1:4100
```

当前本地运行配置是：

```json
{
  "agxHost": "10.29.174.183",
  "tcpPort": 8080,
  "commandUdpPort": 14551,
  "telemetryPort": 14550,
  "rtspPort": 8554,
  "rtspPath": "/m350/fpv",
  "transport": "tcp",
  "heartbeatEnabled": true,
  "autoVideo": true
}
```

该文件位于 `pc-console/bridge/runtime/config.json`，属于本机运行配置，不应作为所有 PC 的固定配置。也可以直接在页面右上角“设置”中修改。

AGX 的 `UDP_TARGET_IP` 必须填写 PC 在机地网络中的实际 IP，而不是 AGX IP。

## 9. 推荐启动顺序

1. 给 M350 和 AGX 上电，确认 E-Port UART 和 USB-C 连接。
2. 启动/重启 Jetson USB Gadget，确认两个 `startup_bulk` 进程存在。
3. 确认 `role=device`、`udc=configured`。
4. 确认 `/dev/ttyTHS0` 未被其他进程占用。
5. 启动 `hpcn-psdk`，观察日志直到 UART、订阅、Liveview、RTSP、TCP/UDP 都成功。
6. 在 PC 启动 AEROLINK 控制台。
7. 先确认遥测在线，再确认视频，最后才进行无桨控制测试。

## 10. 验收命令

### 10.1 AGX 服务

```bash
sudo systemctl status hpcn-psdk --no-pager
sudo journalctl -u hpcn-psdk -f
ss -lntp | grep -E '8080|8554'
ss -lunp | grep 14551
```

期望日志：

```text
RTSP hardware transcoder: 640x360 10 fps 800 kbps IDR=2 s
RTSP server listening at rtsp://0.0.0.0:8554/m350/fpv
Liveview receiver started: camera=7 source=0 ...
```

### 10.2 视频

```bash
ffplay -rtsp_transport tcp rtsp://<AGX_IP>:8554/m350/fpv

ffprobe -v error -rtsp_transport tcp \
  -select_streams v:0 \
  -show_entries stream=codec_name,width,height,r_frame_rate,avg_frame_rate \
  -of default=noprint_wrappers=1 \
  rtsp://<AGX_IP>:8554/m350/fpv
```

期望：H.264、640×360、约 10 fps，连接后约 2～3 秒出画面。

### 10.3 PC 控制台

```bash
cd pc-console
npm test
npm run lint
npm run build
```

已完成的本地验证记录：

- 协议单元测试：3/3 通过。
- ESLint：通过。
- PC 生产构建：通过。
- npm 生产依赖审计：0 个已知漏洞。
- Mock UDP 遥测：已验证能进入 Bridge 并更新前端状态。

本次开发机无法访问现场 AGX `10.29.174.183`，因此 PC→真实 AGX 的最终视频、遥测和飞行控制联调仍需在机地网络中完成。AGX C++ 也应在实际 JetPack 环境重新 clean build。

## 11. 已知问题和后续工作

按优先级排序：

1. **建立 Git 基线。** 当前 0.2.1 没有版本历史，继续复制目录会造成版本失控。
2. **修正文档旧路径。** `doc/LIVEVIEW_RECEIVER.md` 和 `deploy/systemd/README.md` 部分命令仍写 `HPCN_PSDK_Control-0.2.0`，实际应为 0.2.1。
3. **统一 systemd 生产默认值。** 视频部署必须显式设置 `LIVEVIEW_ENABLE=1`、`PSDK_USB_BULK_REQUIRED=1`。
4. **完成真实联调和飞行安全测试。** 顺序应为无桨测试、系留低高度测试、有限范围任务测试。
5. **增加命令 ACK。** 当前网络写入成功不等于 DJI 动作成功。
6. **修复 TCP 流式拆包。** AGX 当前接收实现仍存在拆包/粘包风险。
7. **增加鉴权和安全机制。** 协议目前没有认证、加密、序号、防重放和幂等控制。
8. **改善 HEIGHTTO。** 当前可能切换 GPS/RTK 高度源，缺少数据新鲜度检查；GPS 噪声也可能无法稳定满足 ±0.3 m 阈值。
9. **降低 PC 视频延迟。** 当前 HLS 约 2～4 秒；需要亚秒级时改用 WebRTC。
10. **清理生成物。** 不要把 `node_modules`、缓存、测试录像和本机 runtime 配置提交到仓库。
11. **修正 CMake 注释。** Jetson CMake 中仍有“只封装、不转码”的旧注释，实际 `rtsp_liveview_server.cpp` 已使用 NVIDIA 硬件解码、缩放和重编码。

## 12. 建议建立交付包

在代码父目录执行，生成不含依赖、缓存、构建物和测试录像的源码包：

```bash
cd /Users/ergou/Code
tar \
  --exclude='HPCN_PSDK_Control-0.2.1/pc-console/node_modules' \
  --exclude='HPCN_PSDK_Control-0.2.1/pc-console/.vinext' \
  --exclude='HPCN_PSDK_Control-0.2.1/pc-console/.wrangler' \
  --exclude='HPCN_PSDK_Control-0.2.1/pc-console/.next' \
  --exclude='HPCN_PSDK_Control-0.2.1/pc-console/bridge/runtime' \
  --exclude='HPCN_PSDK_Control-0.2.1/build*' \
  --exclude='HPCN_PSDK_Control-0.2.1/m350-fpv.h264' \
  -czf HPCN_PSDK_Control-0.2.1-handoff.tar.gz \
  HPCN_PSDK_Control-0.2.1
```

## 13. 接手检查清单

- [ ] 已确认使用 `HPCN_PSDK_Control-0.2.1`，不是 0.2.0。
- [ ] 已初始化 Git 并保存交接基线。
- [ ] 已在 AGX clean build USB Bulk 版本。
- [ ] 已核对 PSDK App 信息和现场授权配置。
- [ ] 已核对 E-Port UART 电平、接线和 `/dev/ttyTHS0`。
- [ ] 已确认 `startup_bulk`、FunctionFS 端点、USB role/UDC。
- [ ] 已将 `UDP_TARGET_IP` 设置为 PC 实际 IP。
- [ ] 已启用 `LIVEVIEW_ENABLE=1` 和 `PSDK_USB_BULK_REQUIRED=1`。
- [ ] 已验证 RTSP 640×360、10 fps、约 800 kbps。
- [ ] 已验证 PC 遥测、心跳、视频和控制命令。
- [ ] 已完成无桨和系留安全测试。

## 14. 关联文档

- AGX Liveview/RTSP：`doc/LIVEVIEW_RECEIVER.md`
- 飞行控制协议：`doc/CONTROL_COMMAND_PROTOCOL.md`
- systemd 部署：`deploy/systemd/README.md`
- PC 使用：`pc-console/README.md`
- PC 架构：`pc-console/docs/ARCHITECTURE.md`
