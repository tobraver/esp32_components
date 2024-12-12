import socket
import struct
import time
import json
import gzip
import base64
import os
import wave

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

def send_wav_multicast(filename, multicast_group, port, chunk_size, cache_size, frame_delay):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    
    ttl = struct.pack('b', 1)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, ttl)
    
    try:
        w_len = 0
        with open(filename, 'rb') as f:
            while True:
                data = f.read(chunk_size)
                if not data:
                    break
                    
                sock.sendto(data, (multicast_group, port))
                
                w_len += len(data)
                if w_len > cache_size:  # wav
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
    mac_list = ["e4b06385e750"]
    
    WAV_FILE = "qing3.wav"
    
    # 读取WAV文件信息
    with wave.open(WAV_FILE, 'rb') as wav_file:
        channels = wav_file.getnchannels()
        sample_width = wav_file.getsampwidth()
        frame_rate = wav_file.getframerate()
        n_frames = wav_file.getnframes()
        duration = n_frames / float(frame_rate)
        
    file_size = os.path.getsize(WAV_FILE)
    print(f"文件大小: {file_size/1024/1024:.2f} MB ({file_size} 字节)")
    
    minutes = int(duration // 60)
    seconds = duration % 60
    print(f"音频时长: {minutes}分 {seconds:.2f}秒")
    print(f"采样率: {frame_rate}")
    print(f"声道数: {channels}")
    print(f"采样位数: {sample_width * 8}")
    
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
                "format": 1,  # WAV格式为1
                "rate": frame_rate,
                "channel": channels,
                "bits": sample_width * 8,
                "bit_rate": frame_rate * channels * sample_width * 8,  # WAV比特率计算
                "mac": compressed_mac
            },
            "response": {
                "ip": "192.168.1.121",
                "port": 8569
            }
        }
    }
    
    MULTICAST_GROUP = "239.205.155.251"
    PORT = 8000
    send_start_multicast(MULTICAST_GROUP, PORT, start_json) 
    
    time.sleep(3)
    
    MULTICAST_GROUP = "239.205.155.252"
    PORT = 9999
    
    # WAV的缓存大小和帧延迟计算
    bit_rate = frame_rate * channels * sample_width * 8
    cache_size = bit_rate / 8 * 5  # 5秒缓存
    frame_delay = (duration - 15) * 1.0 / ((file_size - cache_size) / 512.0)
    
    print(f"缓冲数据: {cache_size}字节, 帧间间隔: {frame_delay:.4f}秒")
    print(f"开始发送WAV文件到 {MULTICAST_GROUP}:{PORT}")
    send_wav_multicast(WAV_FILE, MULTICAST_GROUP, PORT, 512, cache_size, frame_delay)
    print("发送完成") 