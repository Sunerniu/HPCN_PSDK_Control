import socket
import json
import time


def get_bind_config():
    """
    从命令行读取绑定 IP 和端口
    如果用户直接回车，则使用默认值
    """
    default_ip = "0.0.0.0"
    default_port = 18080

    ip_input = input(f"请输入监听 IP（默认 {default_ip}）: ").strip()
    port_input = input(f"请输入监听端口（默认 {default_port}）: ").strip()

    bind_ip = ip_input if ip_input else default_ip

    if port_input:
        try:
            bind_port = int(port_input)
            if not (0 <= bind_port <= 65535):
                raise ValueError("端口号超出范围")
        except ValueError:
            print(f"[WARN] 端口输入非法，已使用默认端口 {default_port}")
            bind_port = default_port
    else:
        bind_port = default_port

    return bind_ip, bind_port


def main():
    # 从输入行获取绑定参数
    bind_ip, bind_port = get_bind_config()

    # 创建 UDP Socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # 允许端口复用（防止脚本重启时提示端口被占用）
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    try:
        sock.bind((bind_ip, bind_port))
        print("========== 无人机心跳接收端 ==========")
        print(f"[INFO] 正在监听 UDP {bind_ip}:{bind_port} ...")
        print("按 Ctrl+C 退出监听")
        print("======================================\n")

        while True:
            # 接收数据
            data, addr = sock.recvfrom(1024)

            try:
                # 将字节流解码为 UTF-8 字符串并解析 JSON
                json_str = data.decode('utf-8')
                payload = json.loads(json_str)

                # 提取时间戳并转换为可读时间格式
                ts = payload.get('timestamp', 0)
                local_time = time.strftime('%H:%M:%S', time.localtime(ts))

                # 格式化输出接收到的信息
                print(f"[{local_time}] 收到来自 {addr[0]}:{addr[1]} 的心跳:")
                print(f"  ▶ 无人机 ID : {payload.get('uav_id')}")
                print(f"  ▶ 消息类型  : {payload.get('msg_type')}")
                print(f"  ▶ 当前状态  : {payload.get('status')}")
                print(f"  ▶ 安全限制  : {payload.get('safety_limits')}")
                print("-" * 45)

            except json.JSONDecodeError:
                print(f"[WARN] 解析 JSON 失败，收到原始数据: {data}")
            except Exception as e:
                print(f"[ERROR] 处理数据时发生错误: {e}")

    except KeyboardInterrupt:
        print("\n[INFO] 停止监听。")
    except Exception as e:
        print(f"[ERROR] 绑定或监听失败: {e}")
    finally:
        sock.close()


if __name__ == "__main__":
    main()