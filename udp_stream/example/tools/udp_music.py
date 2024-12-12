import socket
import struct
import time

def send_mp3_multicast(filename, multicast_group, port, chunk_size=1024):
    # 创建UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    
    # 设置TTL
    ttl = struct.pack('b', 1)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, ttl)
    
    try:
        # 打开MP3文件
        with open(filename, 'rb') as f:
            while True:
                # 读取数据块
                data = f.read(chunk_size)
                if not data:
                    break
                    
                # 发送数据到组播地址
                sock.sendto(data, (multicast_group, port))
                
                # 控制发送速率，避免网络拥塞
                time.sleep(0.04)
                
    except FileNotFoundError:
        print(f"找不到文件: {filename}")
    except Exception as e:
        print(f"发送错误: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    # 组播地址和端口
    MULTICAST_GROUP = "239.205.155.250"
    PORT = 9999
    
    # MP3文件路径
    MP3_FILE = "test.mp3"  # 替换为你的MP3文件路径
    
    print(f"开始发送MP3文件到 {MULTICAST_GROUP}:{PORT}")
    send_mp3_multicast(MP3_FILE, MULTICAST_GROUP, PORT)
    print("发送完成") 