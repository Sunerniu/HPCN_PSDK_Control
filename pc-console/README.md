# AEROLINK M350 PC 任务控制台

这是运行在控制 PC 上的本地 Web 控制台，不运行在 Jetson AGX 上。

浏览器负责操作界面，本机 Node.js 桥接服务负责：

- 通过 TCP 8080 或 UDP 14551 向 AGX 发送 30 字节控制包；
- 每秒向 AGX UDP 14551 发送 JSON 心跳；
- 在 PC UDP 14550 接收 AGX 遥测；
- 读取 `rtsp://<AGX_IP>:8554/m350/fpv`；
- 使用 FFmpeg 将 RTSP H.264 无转码封装为浏览器可播放的 HLS。

## 前置条件

- Node.js 22.13 或更高版本；
- FFmpeg 在 PC 的 `PATH` 中；
- PC 能访问 AGX 的 8080、14551 和 8554 端口；
- PC 防火墙允许接收 UDP 14550；
- AGX 启动程序时将 `UDP_TARGET_IP` 配置成 **PC 的实际 IP**。

macOS：

```bash
brew install ffmpeg
```

Ubuntu：

```bash
sudo apt update
sudo apt install ffmpeg
```

## 启动

```bash
npm install
npm run dev
```

浏览器打开 `http://localhost:3000`。本地桥接 API 监听 `http://127.0.0.1:4100`。

首次打开后，在右上角设置中确认 AGX IP。默认值为 `10.29.174.183`。

## AGX 启动配置要点

假设 PC 地址是 `10.29.213.243`：

```bash
UDP_TARGET_IP=10.29.213.243
LIVEVIEW_ENABLE=1
LIVEVIEW_CAMERA=fpv
LIVEVIEW_SOURCE=default
RTSP_ENABLE=1
RTSP_PORT=8554
RTSP_PATH=/m350/fpv
```

不要把 `UDP_TARGET_IP` 设置成 AGX 自己的地址，否则 PC 收不到 UDP 遥测。

## 控制方式

默认使用 UDP 下发控制指令。它与 AGX 的 JSON 心跳链路一致，不需要维持连接。

选择 TCP 时，桥接服务会维持一条长连接。不要用“每个指令连接一次、随后断开”的方式，因为 AGX 当前实现会把 TCP 断线视为控制端离线，并可能暂停导航。

## 视频方式

浏览器不能直接播放 RTSP。桥接服务执行以下无转码流程：

```text
AGX RTSP H.264 → FFmpeg copy/remux → 本机 HLS → 浏览器 H.264 解码
```

这不会在 PC 上再次编码视频，CPU 占用显著低于软件转码，但通常会增加约 2～4 秒播放延迟。需要亚秒级延迟时，下一步应将 PC 端视频桥接升级为 WebRTC。

## 安全说明

- AGX 当前协议没有控制命令 ACK。界面中的“指令已发送”只表示网络写入成功。
- 必须结合飞行状态、导航状态、位置和高度遥测确认实际执行结果。
- 起飞、降落、返航、强制降落、清空任务和切换航点均有二次确认。
- 当前协议无鉴权、加密和防重放，不应跨不可信网络直接使用。
- 实飞前应先进行无桨测试和系留低高度测试。

## 测试与构建

```bash
npm test
npm run build
```
