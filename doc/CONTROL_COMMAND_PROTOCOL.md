# AGX 飞行控制命令与通信协议

本文档描述当前 `HPCN_PSDK_Control-0.2.0` 实际实现的控制接口。所有高度字段均以米为单位。

## 1. 通信接口总览

| 方向 | 协议 | 地址/端口 | 数据格式 | 用途 |
|---|---|---|---|---|
| PC → AGX | TCP | AGX IP:8080 | 30 字节二进制控制包 | 远程下发控制命令 |
| PC → AGX | UDP | AGX IP:14551 | 30 字节二进制控制包 | 远程下发控制命令 |
| PC → AGX | UDP | AGX IP:14551 | JSON | 心跳 |
| AGX → PC | UDP | `UDP_TARGET_IP`:14550 | UTF-8 JSON | 飞行状态、时间和定位遥测 |
| 本机 → AGX 程序 | stdin | 本地终端 | 文本命令 | 调试和人工控制 |

TCP 与 UDP 二进制命令使用完全相同的包结构和命令码。

当前协议限制：

- TCP/UDP 命令执行后不返回 ACK 或结果包。
- `STATUS` 只在 AGX 本地日志中打印状态，不会通过 TCP/UDP回复。
- 校验和字段已保留，但接收端目前没有强制校验。
- 协议没有序列号、重放保护、身份认证或加密。
- TCP 接收端当前按单次 `recv()` 必须正好得到 30 字节处理。发送端应使用 `sendall()`，但正式使用前仍建议修复 TCP 拆包/粘包处理。

## 2. 二进制控制包

### 2.1 字节布局

总长度固定为 30 字节，小端序。

Python `struct` 格式：

```text
<HBBddffH
```

| 偏移 | 长度 | 类型 | 字段 | 说明 |
|---:|---:|---|---|---|
| 0 | 2 | uint16 | `header` | 固定为 `0xAA55` |
| 2 | 1 | uint8 | `cmdType` | 命令码 |
| 3 | 1 | uint8 | `reserved` | 保留，填写 0 |
| 4 | 8 | double | `latitude` | 纬度；部分命令复用 |
| 12 | 8 | double | `longitude` | 经度；部分命令复用 |
| 20 | 4 | float | `altitude` | 绝对高度；部分命令复用 |
| 24 | 4 | float | `speed` | 水平速度；部分命令复用 |
| 28 | 2 | uint16 | `checksum` | 前 28 字节逐字节求和后截取低 16 位；当前可填写 0 |

### 2.2 Python 组包示例

```python
import socket
import struct

HEADER = 0xAA55
PACKET_FORMAT = "<HBBddffH"

def build_packet(cmd, latitude=0.0, longitude=0.0,
                 altitude=0.0, speed=0.0):
    first_28 = struct.pack(
        "<HBBddff",
        HEADER,
        cmd,
        0,
        latitude,
        longitude,
        altitude,
        speed,
    )
    checksum = sum(first_28) & 0xFFFF
    return first_28 + struct.pack("<H", checksum)

# TCP 下发 heightto 绝对高度 65 m
with socket.create_connection(("10.29.174.183", 8080)) as sock:
    sock.sendall(build_packet(0x17, altitude=65.0))

# UDP 下发相同命令
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    sock.sendto(
        build_packet(0x17, altitude=65.0),
        ("10.29.174.183", 14551),
    )
```

## 3. 可下发命令

### 3.1 基础飞行命令

| 命令码 | 名称 | 有效字段 | 当前行为 |
|---:|---|---|---|
| `0x01` | `TAKEOFF` | 无 | 获取控制权并执行 DJI 默认自动起飞；不接受目标高度 |
| `0x02` | `LAND` | 无 | 清除导航任务后自动降落 |
| `0x03` | `GOHOME` | 无 | 清除导航任务后返航 |
| `0x04` | `CANCEL_GOHOME` | 无 | 取消返航 |
| `0x05` | `HOVER` | 无 | 清除导航任务并发送零速度悬停 |
| `0x06` | `CONFIRM_LAND` | 无 | 确认降落 |
| `0x07` | `FORCE_LAND` | 无 | 清除导航任务并强制降落 |
| `0x08` | `CANCEL_LAND` | 无 | 取消降落 |

`TAKEOFF` 使用 DJI 默认起飞流程。即使二进制包中填写了 `altitude`，当前实现也会忽略该字段。

### 3.2 导航与高度命令

| 命令码 | 名称 | 有效字段 | 当前行为 |
|---:|---|---|---|
| `0x13` | `PLANSTO` | `latitude`, `longitude`, `altitude`, `speed` | 向队列追加航点并开始/继续执行 |
| `0x15` | `CHANGESTO` | `latitude`, `longitude`, `altitude`, `speed` | 清除当前目标和队列，立即切换到新航点 |
| `0x17` | `HEIGHTTO` | `altitude` | 保持水平位置和航向，只调整到指定绝对高度 |
| `0x20` | `NAV_START` | 无 | 启动已有导航任务 |
| `0x21` | `NAV_STOP` | 无 | 暂停导航并悬停，可恢复 |
| `0x22` | `NAV_PAUSE` | 无 | 与 `NAV_STOP` 相同 |
| `0x23` | `NAV_RESUME` | 无 | 恢复处于暂停状态的任务 |
| `0x24` | `NAV_CLEAR` | 无 | 清空当前任务和队列并悬停 |

航点参数：

- `latitude`：`[-90, 90]` 度。
- `longitude`：`[-180, 180]` 度。
- 当前点到目标点的水平距离不得超过 2000 m。
- `altitude`：当前定义为绝对高度，允许范围 `[0, 1500]` m。
- `speed`：航点水平最大速度；填写 `0` 时内部默认使用 5 m/s。
- TCP/UDP 航点命令必须明确填写正确的绝对高度。不要使用 `altitude=0` 表示“保持当前高度”，因为网络命令不会自动替换为当前高度。

`HEIGHTTO` 参数及行为：

- 只使用 `altitude`；`latitude`、`longitude`、`speed` 均被忽略。
- 只允许飞机已经处于空中时执行。
- 获取 PSDK 摇杆控制权后开始控制。
- 水平速度命令为 `0`，并开启 DJI Stable 模式以保持水平位置。
- yaw 角速度命令为 `0`，不主动改变航向。
- 垂直最大速度为 1.5 m/s；接近目标时按高度误差自动减速。
- 当前到达阈值为 `±0.3 m`。
- `HEIGHTTO`、`PLANSTO` 和 `CHANGESTO` 会相互替换，不会同时执行。

### 3.3 系统和云台命令

| 命令码 | 名称 | 有效字段 | 当前行为 |
|---:|---|---|---|
| `0x30` | `STATUS` | 无 | 在 AGX 本地日志打印状态 |
| `0x31` | `AUTH` | 无 | 获取 PSDK 摇杆控制权 |
| `0x3F` | `HEARTBEAT` | 无 | 二进制空操作命令，不更新 UDP JSON 心跳计时器 |
| `0x40` | `GIMBAL_ROTATE` | 复用四个数值字段 | 旋转云台 |
| `0x41` | `GIMBAL_RESET` | 无 | 云台复位 |

`GIMBAL_ROTATE` 字段复用：

| 控制包字段 | 云台含义 |
|---|---|
| `latitude` | pitch，度 |
| `longitude` | roll，度 |
| `altitude` | yaw，度 |
| `speed` | 动作时间，秒 |

## 4. UDP JSON 心跳

发送到 AGX UDP `14551`。接收端只检查报文以 `{` 开头并包含字符串 `"heartbeat"`。

推荐每秒发送一次：

```json
{
  "msg_type": "heartbeat",
  "timestamp": 1784822400,
  "uav_id": 1,
  "status": "alive"
}
```

当前超时行为：

- 从未收到过心跳：不会启动心跳失联处理。
- 已经收到过心跳，之后连续 10 秒未收到：暂停导航。
- 连续 200 秒未收到：执行返航。
- 二进制 `CMD_HEARTBEAT (0x3F)` 不会刷新这个 JSON 心跳计时器。

## 5. 本地控制台命令

```text
takeoff
t
heightto <绝对高度>

land
l
home
gohome
cancelhome
hover
confirmland
forceland
cancelland

plansto <纬度> <经度> [绝对高度] [速度]
changesto <纬度> <经度> [绝对高度] [速度]
start
stop
resume
clear

status
auth
gimbal_rotate <pitch> <roll> <yaw> [时间]
gimbal_reset
help
h
quit
exit
q
```

控制台的 `plansto/changesto` 如果省略高度，会读取下发时的当前绝对高度。TCP/UDP 二进制接口没有这个自动补全行为。

## 6. AGX → PC 遥测

AGX 启动时读取环境变量 `UDP_TARGET_IP`，之后始终向该固定 IP 的
UDP `14550` 端口发送状态包。控制指令和心跳不会修改这个地址。未设置
`UDP_TARGET_IP` 时使用默认地址 `192.168.0.1`。

例如，将状态包固定发送到控制端 `10.29.174.100`：

```bash
UDP_TARGET_IP=10.29.174.100
```

UDP JSON 状态包：

- 快包：默认每 100 ms 一次。
- 慢包：默认每 5000 ms 一次。
- 这两种报文都不是控制命令 ACK。

快包主要字段：

```json
{
  "packet_time": 1784822400,
  "GPS_time": 1784822400123456,
  "position_source": "RTK Available",
  "gps": {
    "valid": true,
    "lat": 22.0000000,
    "lon": 113.0000000,
    "alt": 52.30,
    "fix_state": 3
  },
  "rtk": {
    "valid": true,
    "lat": 22.0000000,
    "lon": 113.0000000,
    "alt": 52.31,
    "position_info": 50
  }
}
```

- `packet_time`：AGX 调用 `time(NULL)` 得到的 Unix 秒。
- `GPS_time`：位置订阅时间转换后的 Unix 微秒；时间同步不可用时为 `null`。

慢包包含 `packet_time`、GPS/RTK 位置，并增加姿态、速度、电池、飞行状态、融合高度、云台、导航队列和：

```json
{
  "precise_position": {
    "valid": true,
    "GPS_precise_time": 1784822400123456,
    "lat": 22.0000000000,
    "lon": 113.0000000000,
    "alt": 52.310,
    "solution": 50
  }
}
```

注意：当前慢包没有顶层 `GPS_time` 字段；精确事件时间只在
`precise_position.GPS_precise_time` 中。快包才包含顶层 `GPS_time`。

精准定位查询默认关闭，以免飞机或当前接入口不支持
`DjiPositioning_GetPositionInformationSync()` 时每秒产生超时重试日志。
PPS 时间同步及快包 `GPS_time` 不受影响。确认 RTK 状态和接口支持后，可在
启动时设置：

```bash
ENABLE_PRECISE_POSITIONING=1
```

未启用时，慢包中的 `precise_position.valid` 为 `false`。

## 7. 当前没有开放的远程命令

下列能力虽然部分存在于内部 C++ 函数中，但当前没有对应的 TCP/UDP
命令码：

- 直接下发 `vx/vy/vz/yawRate`。
- `takeoff <高度>`；起飞高度由 DJI 默认起飞流程决定。
- 为 `HEIGHTTO` 指定垂直速度。
- 主动释放 PSDK 摇杆控制权。
- 修改返航高度、避障开关或 RTK 开关。
- 主动触发一次精准定位查询。

## 8. 当前安全注意事项

已修复：

1. 导航速度发送已使用发送锁和任务版本号串行化；取消任务后，旧任务不能覆盖最后的悬停命令。
2. TCP 断线和 UDP 心跳 10 秒超时现在会暂停任务并立即发送悬停。

仍未修复：

1. `HEIGHTTO` 控制过程中可能在 RTK 与 GPS 高度源之间切换，也没有检查高度数据的新鲜度。
2. GPS 高度噪声通常无法稳定满足 `±0.3 m` 到达阈值。
3. TCP 接收没有实现可靠的流式拆包。
4. 协议目前不强制校验和，也没有 ACK、认证或防重放。

在修复上述问题并完成桨叶拆除测试、系留低高度测试前，不应直接进行无人值守实飞。
