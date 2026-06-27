# 在 G1 PC2 上构建 / 验证

mkdir -p build && cd build && cmake .. && make lerobot_to_xhand

# 无硬件：先起接收端 dry-run，再用 sender 打链路
./lerobot_to_xhand --config ../config.yaml --dry-run          # 终端A：打印 L=CLOSE/open
python ../scripts/lerobot_udp_test_sender.py --fps 30 --period 2   # 终端B：合成开合

# 真实回放一段 parquet（实时推流，非离线预处理）
python ../scripts/lerobot_udp_test_sender.py --parquet <ep>.parquet --fps 30

# 接真手
./lerobot_to_xhand --config ../config.yaml --udp-port 9100