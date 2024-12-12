import socket
import struct
import time
import json
import gzip
import base64
import os
from mutagen.mp3 import MP3

def compress_mac_list(mac_list):
    # 将MAC列表转换为JSON字符串
    json_str = json.dumps(mac_list)
    # 使用gzip压缩
    compressed = gzip.compress(json_str.encode('utf-8'))
    # 转换为base64
    base64_str = base64.b64encode(compressed).decode('utf-8')
    return base64_str

def send_start_multicast(multicast_group, port, json_data):
    # 创建UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    
    # 设置TTL
    ttl = struct.pack('b', 1)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, ttl)
    
    try:
        # 将JSON数据转换为字符串
        data = json.dumps(json_data)
        
        # 发送数据到组播地址
        sock.sendto(data.encode('utf-8'), (multicast_group, port))
        print(f"已发送start指令到 {multicast_group}:{port}")
        
    except Exception as e:
        print(f"发送错误: {e}")
    finally:
        sock.close()


def send_mp3_multicast(filename, multicast_group, port, chunk_size, cache_size, frame_delay):
    # 创建UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    
    # 设置TTL
    ttl = struct.pack('b', 1)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, ttl)
    
    try:
        w_len = 0
        with open(filename, 'rb') as f:
            while True:
                # 读取数据块
                data = f.read(chunk_size)
                if not data:
                    break
                    
                # 发送数据到组播地址
                sock.sendto(data, (multicast_group, port))
                
                w_len += len(data)
                if w_len > cache_size: # mp3
                    # 控制发送速率，避免网络拥塞
                    time.sleep(frame_delay)
                else:
                    time.sleep(0.001)

    except FileNotFoundError:
        print(f"找不到文件: {filename}")
    except Exception as e:
        print(f"发送错误: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    # MAC地址列表
    # mac_list = ["f09e9e0f8254", "f412fafaa9f0", "48ca43332dc4",
    #             "e4b06385ee14", "64e833443248", "64e833443e1c",
    #             "3c8427f2fcf0", "3c8427f0b3a0"]
    
    mac_list = ["e4b06385e750"]
    
    MP3_FILE = "lu.mp3"
    
    # 读取MP3文件信息
    audio = MP3(MP3_FILE)
    file_size = os.path.getsize(MP3_FILE)
    print(f"文件大小: {file_size/1024/1024:.2f} MB ({file_size} 字节)")
    
    duration = audio.info.length
    minutes = int(duration // 60)
    seconds = duration % 60
    print(f"音频时长: {minutes}分 {seconds:.2f}秒")
    print(f"采样率: {audio.info.sample_rate}")
    print(f"声道数: {audio.info.channels}")
    print(f"比特率: {audio.info.bitrate}")
    
    # 压缩MAC列表
    compressed_mac = compress_mac_list(mac_list)
    
    # 构建JSON数据
    start_json = {
        "task_id": "12345613",
        "method": "start",
        "params": {
            "music": {
                "ip": "239.205.155.252",
                "port": 9999,
                "format": 0,
                "rate": audio.info.sample_rate,  # 采样率
                "channel": audio.info.channels,  # 声道数
                "bits": 16,  # MP3通常是16位
                "bit_rate": audio.info.bitrate,  # 比特率
                "mac": compressed_mac
            },
            "response": {
                "ip": "192.168.1.121",
                "port": 8569
            }
        }
    }
    
    # 控制 组播地址和端口
    MULTICAST_GROUP = "239.205.155.251"
    PORT = 8000
    send_start_multicast(MULTICAST_GROUP, PORT, start_json) 
    
    time.sleep(3)
    
    # 音乐 组播地址和端口
    MULTICAST_GROUP = "239.205.155.252"
    PORT = 9999
    
    cache_size = audio.info.bitrate / 8 * 5
    # frame_delay = (audio.info.length - 5) * 1.0 / ((file_size - cache_size) / 512.0) #理论
    frame_delay = (audio.info.length - 15) * 1.0 / ((file_size - cache_size) / 512.0) #实际, 略快于5秒, 防止后续播放断断续续
    print(f"缓冲数据: {cache_size}字节, 帧间间隔: {frame_delay:.4f}秒")
    print(f"开始发送MP3文件到 {MULTICAST_GROUP}:{PORT}")
    send_mp3_multicast(MP3_FILE, MULTICAST_GROUP, PORT, 512, cache_size, frame_delay)
    print("发送完成") 