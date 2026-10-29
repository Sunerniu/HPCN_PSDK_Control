# HPCN PSDK 通信协议

本协议适用于关闭精确定位的配置：

```text
ENABLE_PRECISE_POSITIONING=0
```

## 1. 通信端口

| 方向 | 协议 | 地址 | 内容 |
|---|---|---|---|
| 控制端 → AGX | TCP | `AGX_IP:8080` | 30 字节二进制控制包，保持长连接 |
| 控制端 → AGX | UDP | `AGX_IP:14551` | 30 字节二进制控制包或 JSON 心跳 |
| AGX → 控制端 | UDP | `UDP_TARGET_IP:14550` | JSON 状态包 |

TCP 和 UDP 使用相同的二进制命令格式。TCP 当前只处理一个客户端。

## 2. 二进制控制包

固定长度 30 字节，小端序。

Python `struct` 格式：

```text
<HBBddffH
```

| 偏移 | 类型 | 字段 | 说明 |
|---:|---|---|---|
| 0 | `uint16` | `header` | 固定为 `0xAA55` |
| 2 | `uint8` | `cmdType` | 命令码 |
| 3 | `uint8` | `reserved` | 填写 `0` |
| 4 | `double` | `latitude` | 纬度或复用参数 |
| 12 | `double` | `longitude` | 经度或复用参数 |
| 20 | `float` | `altitude` | 绝对海拔或复用参数，单位 m |
| 24 | `float` | `speed` | 速度或复用参数，单位 m/s |
| 28 | `uint16` | `checksum` | 前 28 字节之和的低 16 位 |

当前接收端检查包长和 `header`，暂未强制检查 `checksum`。

组包示例：

```python
import struct

def build_packet(cmd, latitude=0.0, longitude=0.0,
                 altitude=0.0, speed=0.0):
    body = struct.pack(
        "<HBBddff",
        0xAA55,
        cmd,
        0,
        latitude,
        longitude,
        altitude,
        speed,
    )
    checksum = sum(body) & 0xFFFF
    return body + struct.pack("<H", checksum)
```

## 3. 可接收命令

### 3.1 飞行命令

| 命令码 | 名称 | 参数 | 行为 |
|---:|---|---|---|
| `0x01` | `TAKEOFF` | 无 | 获取控制权，执行 DJI 默认起飞 |
| `0x02` | `LAND` | 无 | 清除导航并降落 |
| `0x03` | `GOHOME` | 无 | 清除导航并返航 |
| `0x04` | `CANCEL_GOHOME` | 无 | 取消返航 |
| `0x05` | `HOVER` | 无 | 清除导航并发送零速度悬停 |
| `0x06` | `CONFIRM_LAND` | 无 | 确认降落 |
| `0x07` | `FORCE_LAND` | 无 | 清除导航并强制降落 |
| `0x08` | `CANCEL_LAND` | 无 | 取消降落 |

### 3.2 导航与高度命令

| 命令码 | 名称 | 参数 | 行为 |
|---:|---|---|---|
| `0x13` | `PLANSTO` | `latitude, longitude, altitude, speed` | 追加航点并执行 |
| `0x15` | `CHANGESTO` | `latitude, longitude, altitude, speed` | 清除原任务并切换到新航点 |
| `0x17` | `HEIGHTTO` | `altitude` | 保持水平位置和航向，飞至指定绝对海拔 |
| `0x20` | `NAV_START` | 无 | 启动已有导航任务 |
| `0x21` | `NAV_STOP` | 无 | 暂停导航并悬停 |
| `0x22` | `NAV_PAUSE` | 无 | 暂停导航并悬停 |
| `0x23` | `NAV_RESUME` | 无 | 恢复暂停的导航任务 |
| `0x24` | `NAV_CLEAR` | 无 | 清空导航任务并悬停 |

参数限制：

- 纬度：`[-90, 90]`。
- 经度：`[-180, 180]`。
- 目标点距离当前位置不超过 `2000 m`。
- `altitude` 为绝对海拔，范围 `[0, 1500] m`。
- 航点 `speed=0` 时内部默认使用 `5 m/s`。
- `HEIGHTTO` 只在飞机已经处于空中时执行。
- `HEIGHTTO` 最大垂直速度固定为 `1.5 m/s`。
- `HEIGHTTO` 只使用 `altitude`，忽略其他参数。

### 3.3 系统与云台命令

| 命令码 | 名称 | 参数 | 行为 |
|---:|---|---|---|
| `0x30` | `STATUS` | 无 | 在 AGX 日志打印状态 |
| `0x31` | `AUTH` | 无 | 获取 PSDK 摇杆控制权 |
| `0x3F` | `HEARTBEAT` | 无 | TCP 二进制心跳，不执行飞行动作 |
| `0x40` | `GIMBAL_ROTATE` | 见下表 | 旋转云台 |
| `0x41` | `GIMBAL_RESET` | 无 | 云台复位 |

`GIMBAL_ROTATE` 参数复用：

| 控制包字段 | 含义 |
|---|---|
| `latitude` | pitch，单位 ° |
| `longitude` | roll，单位 ° |
| `altitude` | yaw，单位 ° |
| `speed` | 动作时间，单位 s |

## 4. 心跳

### 4.1 TCP

TCP 客户端保持长连接，建议每秒发送一次二进制：

```text
CMD_HEARTBEAT = 0x3F
```

TCP 断开超过 10 秒且正在导航时，AGX 停止导航并发送零速度悬停。

### 4.2 UDP

UDP 客户端建议每秒向 `AGX_IP:14551` 发送一次：

```json
{
  "msg_type": "heartbeat",
  "timestamp": 1784822400000,
  "status": "alive"
}
```

失联处理：

- 10 秒没有 UDP JSON 心跳：停止导航并悬停。
- 200 秒没有 UDP JSON 心跳：执行返航。
- 心跳恢复：重置失联状态和计时。

二进制 `0x3F` 不刷新 UDP 心跳计时；UDP 必须发送 JSON 心跳。

## 5. AGX 状态发包

AGX 使用 UDP 向固定的 `UDP_TARGET_IP:14550` 发送 UTF-8 JSON。

### 5.1 快包

发送周期：默认 `100 ms`。

```json
{
  "packet_time": 1784822400,
  "GPS_time": 1784822400123456,
  "position_source": "RTK Available",
  "gps": {
    "valid": true,
    "lat": 39.9630953,
    "lon": 116.3525050,
    "alt": 49.29,
    "fix_state": 3
  },
  "rtk": {
    "valid": true,
    "lat": 39.9631121,
    "lon": 116.3524884,
    "alt": 40.33,
    "position_info": 50
  }
}
```

字段说明：

- `packet_time`：AGX Unix 时间，单位秒。
- `GPS_time`：位置数据对应的 Unix 时间，单位微秒；时间同步不可用时为 `null`。
- `position_source`：`RTK Available`、`GPS` 或 `Unavailable`。
- `gps.valid`：GPS 位置是否有效。
- `rtk.valid`：RTK 位置是否有效。

### 5.2 慢包

发送周期：默认 `5000 ms`。

```json
{
  "packet_time": 1784822400,
  "attitude": {
    "pitch": 0.0,
    "roll": 0.0,
    "yaw": 0.0
  },
  "velocity": {
    "vx": 0.0,
    "vy": 0.0,
    "vz": 0.0
  },
  "battery": {
    "voltage": 52000,
    "current": 0,
    "percent": 80,
    "capacity": 0
  },
  "flight_status": 0,
  "display_mode": 0,
  "height": 0.0,
  "home_set": 1,
  "quaternion": {
    "q0": 1.0,
    "q1": 0.0,
    "q2": 0.0,
    "q3": 0.0
  },
  "gimbal": {
    "pitch": 0.0,
    "roll": 0.0,
    "yaw": 0.0
  },
  "position_source": "RTK Available",
  "gps": {
    "valid": true,
    "lat": 39.9630953,
    "lon": 116.3525050,
    "alt": 49.29,
    "fix_state": 3
  },
  "rtk": {
    "valid": true,
    "lat": 39.9631121,
    "lon": 116.3524884,
    "alt": 40.33,
    "position_info": 50
  },
  "precise_position": {
    "valid": false,
    "GPS_precise_time": null
  },
  "navigation": {
    "status": "IDLE",
    "is_navigating": false,
    "has_target": false,
    "last_error": 0,
    "current_target": {
      "lat": 0.0,
      "lon": 0.0,
      "alt": 0.0,
      "speed": 0.0
    },
    "queue_count": 0,
    "queue": []
  }
}
```

`flight_status`：

- `0`：地面，电机停止。
- `1`：地面，电机旋转。
- `2`：飞机在空中。

关闭精确定位后：

```json
"precise_position": {
  "valid": false,
  "GPS_precise_time": null
}
```

慢包不包含顶层 `GPS_time`；顶层 `GPS_time` 只在快包中发送。

## 6. 当前协议限制

- TCP/UDP 控制命令没有 ACK 或执行结果返回。
- `STATUS` 只打印到 AGX 日志。
- 没有身份认证、加密、来源 IP 白名单和防重放。
- 校验和字段暂未强制验证。
