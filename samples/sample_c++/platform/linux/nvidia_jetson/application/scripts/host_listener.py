import socket
import json
import time

# 配置
BIND_IP = "0.0.0.0" # 监听本机所有网卡
BIND_PORT = 14550   # 监听端口
BUFFER_SIZE = 4096

def main():
    # 创建UDP Socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    try:
        # 绑定端口
        sock.bind((BIND_IP, BIND_PORT))
        print(f"[INFO] 正在监听 UDP 端口 {BIND_PORT} ...")
        print(f"[INFO] 请确保 AGX 已配置向本机 IP 发送数据")
        print("-" * 50)
        
        while True:
            # 接收数据
            data, addr = sock.recvfrom(BUFFER_SIZE)
            
            try:
                # 尝试解码为字符串并解析JSON
                json_str = data.decode('utf-8')
                status = json.loads(json_str)
                
                # 打印格式化信息
                print_status(status, addr)
                
            except json.JSONDecodeError:
                print(f"[WARN] 收到非JSON数据来自 {addr}: {data[:50]}")
            except UnicodeDecodeError:
                print(f"[WARN] 收到无法解码的数据来自 {addr}")
                
    except KeyboardInterrupt:
        print("\n[INFO] 用户停止监听")
    except Exception as e:
        print(f"\n[ERROR] 发生错误: {e}")
    finally:
        sock.close()

def print_status(s, addr):
    # 清屏或分隔
    # print("\033[H\033[J", end="") # 如果是在Linux终端可取消注释用来清屏
    
    ts = s.get('packet_time', s.get('timestamp', 0))
    local_time = time.strftime('%H:%M:%S', time.localtime(ts))
    
    pos = s.get('position', {})
    gps = s.get('gps', {})
    rtk = s.get('rtk', {})
    vel = s.get('velocity', {})
    bat = s.get('battery', {})
    f_status = s.get('flight_status', -1)
    d_mode = s.get('display_mode', -1)
    
    print(f"[{local_time}] 来自 {addr[0]}")
    if pos:
        print(f"  位置: Lat {pos.get('lat', 0):.6f}, Lon {pos.get('lon', 0):.6f}, Alt {pos.get('alt_abs', 0):.2f}m")
        print(f"  速度: Vx {vel.get('vx', 0):.2f}, Vy {vel.get('vy', 0):.2f}, Vz {vel.get('vz', 0):.2f} m/s")
        print(f"  状态: 模式 {d_mode} | 飞行状态 {f_status} | 电量 {bat.get('percent', 0)}%")
    else:
        print(f"  GPS: valid={gps.get('valid', False)} Lat {gps.get('lat', 0):.6f}, Lon {gps.get('lon', 0):.6f}, Alt {gps.get('alt', 0):.2f}m")
        print(f"  RTK: valid={rtk.get('valid', False)} Lat {rtk.get('lat', 0):.6f}, Lon {rtk.get('lon', 0):.6f}, Alt {rtk.get('alt', 0):.2f}m")
    print("-" * 30)

if __name__ == "__main__":
    main()
