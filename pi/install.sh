#!/bin/bash
# ============================================================
# air-sensor installer for Raspberry Pi
# 用法: sudo bash install.sh
# 可重复执行（重复跑 = 修复/重装）
# ============================================================
set -e

# ---------- 0. 必须 root ----------
if [ "$EUID" -ne 0 ]; then
    echo "[!] 请用 sudo 运行: sudo bash install.sh"
    exit 1
fi

# ---------- 1. 定位脚本所在目录（main.py 同目录） ----------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MAIN_PY="$SCRIPT_DIR/main.py"
REQ_FILE="$SCRIPT_DIR/requirements.txt"
SERVICE_NAME="air-sensor"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"

echo "==> 安装目录: $SCRIPT_DIR"

# ---------- 2. 检查 main.py 存在 ----------
if [ ! -f "$MAIN_PY" ]; then
    echo "[!] 找不到 main.py: $MAIN_PY"
    echo "    请把 install.sh / main.py / requirements.txt 放同一目录"
    exit 1
fi

# ---------- 3. 系统依赖 ----------
echo "==> 安装系统依赖 (python3-pip)..."
apt-get update -qq
apt-get install -y -qq python3-pip python3-serial

# ---------- 4. 普通用户: 加 dialout 组（串口权限） ----------
# SUDO_USER 是你 sudo 之前的普通用户名
TARGET_USER="${SUDO_USER:-pi}"
echo "==> 把用户 $TARGET_USER 加入 dialout 组（串口权限）..."
usermod -aG dialout "$TARGET_USER" || true

# ---------- 5. Python 依赖 ----------
echo "==> 安装 Python 依赖..."
pip3 install --break-system-packages -r "$REQ_FILE"

# ---------- 6. 写 systemd service ----------
echo "==> 写入 systemd 服务: $SERVICE_FILE"
cat > "$SERVICE_FILE" <<EOF
[Unit]
Description=Air Sensor Exporter (UART -> Prometheus)
After=multi-user.target

[Service]
Type=simple
User=$TARGET_USER
WorkingDirectory=$SCRIPT_DIR
ExecStart=/usr/bin/python3 $MAIN_PY
Restart=always
RestartSec=3
# 串口权限兜底
SupplementaryGroups=dialout

[Install]
WantedBy=multi-user.target
EOF

# ---------- 7. 启动 ----------
echo "==> 重启 systemd 并启动服务..."
systemctl daemon-reload
systemctl enable "$SERVICE_NAME"
systemctl restart "$SERVICE_NAME"

sleep 2
echo ""
echo "============================================================"
echo "  安装完成!"
echo "------------------------------------------------------------"
echo "  服务状态:   systemctl status $SERVICE_NAME"
echo "  实时日志:   journalctl -u $SERVICE_NAME -f"
echo "  串口设备:   ls /dev/ttyUSB*"
echo "  指标地址:   curl http://127.0.0.1:8000/metrics"
echo "  重启服务:   sudo systemctl restart $SERVICE_NAME"
echo "  停止服务:   sudo systemctl stop $SERVICE_NAME"
echo "------------------------------------------------------------"
echo "  注意: 加 dialout 组后需要重新登录 SSH 才生效"
echo "        或临时: newgrp dialout"
echo "============================================================"
