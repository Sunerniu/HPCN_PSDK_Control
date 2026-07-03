import socket
import time
import json
import yaml
import os
from typing import Dict, Any


class UdpHeartbeatSender:
    def __init__(
        self,
        config_path: str = "agx_config.yaml",
        heartbeat_interval: float = 1.0,   # 心跳发送周期（秒）
        reload_interval: float = 10.0       # 配置文件重读周期（秒）
    ):
        self.config_path = config_path
        self.heartbeat_interval = heartbeat_interval
        self.reload_interval = reload_interval

        self.uav_agx_mapping: Dict[int, Dict[str, Any]] = {}
        self.safety_limits: Dict[str, Any] = {}

        self.last_reload_time = 0.0
        self.last_send_time = 0.0
        self.last_mtime = None

        # UDP socket
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def load_config(self) -> None:
        """
        读取 YAML 配置文件，并更新当前映射表。
        """
        if not os.path.exists(self.config_path):
            print(f"[WARN] 配置文件不存在: {self.config_path}")
            self.uav_agx_mapping = {}
            self.safety_limits = {}
            return

        try:
            current_mtime = os.path.getmtime(self.config_path)

            # 文件没变，可以不重复加载；如果你想强制每次都加载，可删掉这个判断
            if self.last_mtime is not None and current_mtime == self.last_mtime:
                return

            with open(self.config_path, "r", encoding="utf-8") as f:
                config = yaml.safe_load(f) or {}

            raw_mapping = config.get("uav_agx_mapping", {})
            safety_limits = config.get("safety_limits", {})

            parsed_mapping = {}
            for uav_id, info in raw_mapping.items():
                try:
                    uav_id_int = int(uav_id)
                    ip = info["ip"]
                    port = int(info["port"])
                    parsed_mapping[uav_id_int] = {
                        "ip": ip,
                        "port": port
                    }
                except Exception as e:
                    print(f"[WARN] 跳过非法配置项 uav_id={uav_id}, info={info}, err={e}")

            old_ids = set(self.uav_agx_mapping.keys())
            new_ids = set(parsed_mapping.keys())

            added = new_ids - old_ids
            removed = old_ids - new_ids

            self.uav_agx_mapping = parsed_mapping
            self.safety_limits = safety_limits
            self.last_mtime = current_mtime

            print(f"[INFO] 已重新加载配置文件: {self.config_path}")
            print(f"[INFO] 当前 AGX 映射: {self.uav_agx_mapping}")
            print(f"[INFO] 当前安全限制: {self.safety_limits}")

            if added:
                print(f"[INFO] 新增 UAV 映射: {sorted(added)}")
            if removed:
                print(f"[INFO] 删除 UAV 映射: {sorted(removed)}")

        except Exception as e:
            print(f"[ERROR] 加载配置文件失败: {e}")

    def build_heartbeat_packet(self, uav_id: int) -> bytes:
        """
        构造一个简单的心跳报文。
        这里采用 JSON 文本格式，便于调试和后续扩展。
        """
        packet = {
            "msg_type": "heartbeat",
            "timestamp": int(time.time()),
            "uav_id": uav_id,
            "status": "alive",
            "safety_limits": self.safety_limits
        }
        return json.dumps(packet, ensure_ascii=False).encode("utf-8")

    def send_heartbeats(self) -> None:
        """
        向当前 uav_agx_mapping 中的所有 AGX 发送心跳。
        """
        if not self.uav_agx_mapping:
            print("[INFO] 当前没有可发送的 AGX 目标")
            return

        for uav_id, target in self.uav_agx_mapping.items():
            ip = target["ip"]
            port = target["port"]

            try:
                packet = self.build_heartbeat_packet(uav_id)
                self.sock.sendto(packet, (ip, port))
                print(f"[INFO] 已发送心跳 -> UAV {uav_id}, {ip}:{port}, data={packet.decode('utf-8')}")
            except Exception as e:
                print(f"[ERROR] 发送心跳失败 -> UAV {uav_id}, {ip}:{port}, err={e}")

    def run(self) -> None:
        """
        主循环：
        - 每 reload_interval 秒检查/重载一次配置
        - 每 heartbeat_interval 秒发送一次心跳
        """
        print("[INFO] UDP 心跳发送器启动")
        print(f"[INFO] config_path={self.config_path}")
        print(f"[INFO] heartbeat_interval={self.heartbeat_interval}s")
        print(f"[INFO] reload_interval={self.reload_interval}s")

        # 启动时先加载一次
        self.load_config()
        self.last_reload_time = time.time()
        self.last_send_time = 0.0

        try:
            while True:
                now = time.time()

                # 周期性重读配置
                if now - self.last_reload_time >= self.reload_interval:
                    self.load_config()
                    self.last_reload_time = now

                # 周期性发送心跳
                if now - self.last_send_time >= self.heartbeat_interval:
                    self.send_heartbeats()
                    self.last_send_time = now

                time.sleep(0.2)

        except KeyboardInterrupt:
            print("\n[INFO] 收到 Ctrl+C，程序退出")
        finally:
            self.sock.close()


if __name__ == "__main__":
    sender = UdpHeartbeatSender(
        config_path="agx_config.yaml",
        heartbeat_interval=1.0,   # 心跳周期，可改 单位为秒
        reload_interval=10.0       # 配置重读周期，可改
    )
    sender.run()