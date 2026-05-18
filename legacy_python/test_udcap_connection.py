import socket
import json

PORT = 9000  # 和 UDCAP 里设的端口一致
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", PORT))   # 0.0.0.0 = 接收所有网卡上的包
print(f"Listening on UDP {PORT}...")

while True:
    data, addr = sock.recvfrom(65535)
    try:
        msg = json.loads(data.decode("utf-8"))
        print(f"From {addr[0]}: {len(data)} bytes, keys={list(msg.keys())}")
        # 保存example到本地
        with open("example.json", "w") as f:
            json.dump(msg, f)
            print("Saved example to example.json")
    except Exception as e:
        print(f"Decode error: {e}, raw bytes: {data[:80]}")