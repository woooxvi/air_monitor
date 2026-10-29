# Air Sensor Exporter

C51 空气质量传感器 → CH340 → 树莓派 → Prometheus 指标导出器。

## 硬件接线

```
C51 板子 (5V TTL)          CH340 USB转TTL        树莓派
┌─────────────┐           ┌──────────┐         ┌──────────┐
│        5V   │───────────│ 5V       │         │          │
│        GND  │───────────│ GND      │─────────│ USB      │
│       TXD   │───────────│ RXD      │         │  ttyUSB0 │
│       RXD   │ (烧录时接) │ TXD      │         │          │
└─────────────┘           └──────────┘         └──────────┘
```

- 串口参数：**9600, 8N1**
- CH340 跳线切到 **5V** 档（C51 是 5V TTL）
- 平时只读数据：只接 `TXD → RXD + GND + 5V`
- 烧录固件时临时补接 `CH340.RXD → C51.TXD`

## 传感器帧格式（17 字节）

| 偏移 | 字节 | 说明 |
|---|---|---|
| 0 | 0x3C | 帧头1 |
| 1 | 0x02 | 帧头2 |
| 2-3 | CO2 | 高字节+低字节，单位 ppm |
| 4-5 | CH2O | 甲醛，µg/m³ |
| 6-7 | TVOC | µg/m³ |
| 8-9 | PM2.5 | µg/m³ |
| 10-11 | PM10 | µg/m³ |
| 12 | Temp int | bit7=1 表负温度 |
| 13 | Temp dec | 小数部分 |
| 14 | Humi int | 湿度整数 |
| 15 | Humi dec | 湿度小数 |
| 16 | Checksum | 前 16 字节求和低 8 位 |

## 安装

把这四个文件放同一目录：

```
air-sensor/
├── install.sh
├── main.py
├── requirements.txt
└── README.md
```

在树莓派上执行：

```bash
sudo bash install.sh
```

脚本会自动：
- 安装 `python3-pip` 和系统依赖
- 把当前用户加入 `dialout` 组（串口权限）
- 安装 Python 依赖（pyserial, prometheus-client）
- 创建并启动 systemd 服务 `air-sensor`
- 重复执行 = 修复/重装

## 使用

```bash
# 查看服务状态
systemctl status air-sensor

# 实时日志
journalctl -u air-sensor -f

# 手动重启
sudo systemctl restart air-sensor

# 查看暴露的指标
curl http://127.0.0.1:8000/metrics
```

## Prometheus 配置

在 `prometheus.yml` 的 `scrape_configs` 里加：

```yaml
scrape_configs:
  - job_name: 'air_sensor'
    static_configs:
      - targets: ['<树莓派IP>:8000']
```

Grafana 里可以直接用这些指标画图：

| 指标名 | 含义 |
|---|---|
| `air_co2_ppm` | CO2 ppm |
| `air_ch2o_ugm3` | 甲醛 µg/m³ |
| `air_tvoc_ugm3` | TVOC µg/m³ |
| `air_pm25_ugm3` | PM2.5 |
| `air_pm10_ugm3` | PM10 |
| `air_temp_c` | 温度 °C |
| `air_humi_pct` | 湿度 %RH |
| `air_sensor_up` | 1=在线 0=断线 |
| `air_rx_error_total` | 校验失败计数 |

## 串口设备不对？

```bash
# 插 CH340 后看新设备
ls /dev/ttyUSB*

# 实时看内核识别日志
dmesg | grep ch34
```

如果不是 `/dev/ttyUSB0`，改 `main.py` 顶部的 `SERIAL_PORT`。

## 常见问题

**Permission denied: /dev/ttyUSB0**
```bash
sudo usermod -aG dialout pi
# 然后重新登录 SSH
```

**日志里一直 "串口打开失败"**
- 检查 CH340 是否插好
- `ls /dev/ttyUSB*` 看设备名
- 确认 CH340 跳线在 5V 档

**指标全是 0 或不更新**
- `journalctl -u air-sensor -f` 看有没有收到帧
- 确认 C51 波特率是 9600
- 确认 TX/RX 没接反（C51.TXD → CH340.RXD）
