#!/usr/bin/env python3
# ============================================================
# air-sensor exporter: 读 CH340 串口文本日志 -> 提取上报行 -> Prometheus
# 目标行示例:
# ch2o=   2 tvoc=  14 pm25=   3 pm10=   3 co2= 422 temp=27.2 humi=72.5
# ============================================================
import serial
import time
import logging
import re
from typing import Optional
from prometheus_client import Gauge, start_http_server

# ---------- 配置 ----------
SERIAL_PORT = "/dev/ttyUSB0"
# SERIAL_PORT = "COM15"
BAUD = 9600
METRICS_PORT = 8000

# ---------- 数据清洗阈值 ----------
CO2_MIN = 250
CO2_MAX = 10000
CH2O_MAX = 5000
TVOC_MAX = 5000
PM25_MAX = 2000
PM10_MAX = 2000
TEMP_MIN = -40.0
TEMP_MAX = 85.0
HUMI_MIN = 0.0
HUMI_MAX = 100.0

# 相邻帧突变限幅（约2秒一帧）
MAX_CO2_JUMP = 300
MAX_CH2O_JUMP = 200
MAX_TVOC_JUMP = 500
MAX_PM25_JUMP = 200
MAX_PM10_JUMP = 200
MAX_TEMP_JUMP = 3.0
MAX_HUMI_JUMP = 8.0

# ---------- 日志 ----------
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("air-sensor")

# ---------- Prometheus 指标 ----------
g_co2   = Gauge("air_co2_ppm",    "CO2 concentration ppm")
g_ch2o  = Gauge("air_ch2o_ugm3",  "Formaldehyde ug/m3")
g_tvoc  = Gauge("air_tvoc_ugm3",  "TVOC ug/m3")
g_pm25  = Gauge("air_pm25_ugm3",  "PM2.5 ug/m3")
g_pm10  = Gauge("air_pm10_ugm3",  "PM10 ug/m3")
g_temp  = Gauge("air_temp_c",     "Temperature Celsius")
g_humi  = Gauge("air_humi_pct",   "Humidity %RH")
g_up    = Gauge("air_sensor_up",  "1 if sensor alive, 0 if disconnected")
g_rxerr = Gauge("air_rx_error_total", "Line parse error count")

# ---------- 文本行解析 ----------
REPORT_RE = re.compile(
    r"ch2o=\s*(\d+)\s+"
    r"tvoc=\s*(\d+)\s+"
    r"pm25=\s*(\d+)\s+"
    r"pm10=\s*(\d+)\s+"
    r"co2=\s*(\d+)\s+"
    r"temp=\s*(-?\d+(?:\.\d+)?)\s+"
    r"humi=\s*(-?\d+(?:\.\d+)?)"
)


def parse_report_line(line: str):
    """解析类似: ch2o=2 tvoc=14 ... temp=27.2 humi=72.5 的上报行。"""
    m = REPORT_RE.search(line)
    if not m:
        return None
    return dict(
        ch2o=int(m.group(1)),
        tvoc=int(m.group(2)),
        pm25=int(m.group(3)),
        pm10=int(m.group(4)),
        co2=int(m.group(5)),
        temp=float(m.group(6)),
        humi=float(m.group(7)),
    )


def is_plausible_report(d: dict, last_valid: Optional[dict] = None) -> bool:
    """二次清洗：过滤串口偶发异常帧，避免污染指标。"""
    co2 = d["co2"]
    ch2o = d["ch2o"]
    tvoc = d["tvoc"]
    pm25 = d["pm25"]
    pm10 = d["pm10"]
    temp = d["temp"]
    humi = d["humi"]

    # 现场典型异常1：co2/ch2o 同时为0。
    if co2 == 0 and ch2o == 0:
        return False

    # 现场典型异常2：温湿度同时归零，但污染物数值仍在跳动。
    if temp <= 0.1 and humi <= 0.1:
        return False

    if not (CO2_MIN <= co2 <= CO2_MAX):
        return False
    if not (0 <= ch2o <= CH2O_MAX):
        return False
    if not (0 <= tvoc <= TVOC_MAX):
        return False
    if not (0 <= pm25 <= PM25_MAX):
        return False
    if not (0 <= pm10 <= PM10_MAX):
        return False
    if not (TEMP_MIN <= temp <= TEMP_MAX):
        return False
    if not (HUMI_MIN <= humi <= HUMI_MAX):
        return False

    if last_valid is not None:
        if abs(co2 - last_valid["co2"]) > MAX_CO2_JUMP:
            return False
        if abs(ch2o - last_valid["ch2o"]) > MAX_CH2O_JUMP:
            return False
        if abs(tvoc - last_valid["tvoc"]) > MAX_TVOC_JUMP:
            return False
        if abs(pm25 - last_valid["pm25"]) > MAX_PM25_JUMP:
            return False
        if abs(pm10 - last_valid["pm10"]) > MAX_PM10_JUMP:
            return False
        if abs(temp - last_valid["temp"]) > MAX_TEMP_JUMP:
            return False
        if abs(humi - last_valid["humi"]) > MAX_HUMI_JUMP:
            return False

    return True

# ---------- 串口读取主循环 ----------
def open_serial():
    while True:
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD, timeout=1)
            log.info("串口已打开: %s @ %d", SERIAL_PORT, BAUD)
            return ser
        except Exception as e:
            g_up.set(0)
            log.error("串口打开失败(%s), 3秒后重试...", e)
            time.sleep(3)

def main():
    start_http_server(METRICS_PORT)
    log.info("Prometheus metrics 已启动: http://0.0.0.0:%d/metrics", METRICS_PORT)

    ser = open_serial()
    last_valid = None

    while True:
        try:
            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            result = parse_report_line(line)
            if result is None:
                # 开机调试日志和其他文本行会走到这里，忽略即可。
                continue

            if not is_plausible_report(result, last_valid):
                g_rxerr.inc()
                log.warning(
                    "drop implausible line: co2=%d ch2o=%d tvoc=%d pm25=%d pm10=%d temp=%.1f humi=%.1f",
                    result["co2"], result["ch2o"], result["tvoc"],
                    result["pm25"], result["pm10"], result["temp"], result["humi"],
                )
                continue

            g_co2.set(result["co2"])
            g_ch2o.set(result["ch2o"])
            g_tvoc.set(result["tvoc"])
            g_pm25.set(result["pm25"])
            g_pm10.set(result["pm10"])
            g_temp.set(result["temp"])
            g_humi.set(result["humi"])
            g_up.set(1)
            last_valid = result
            log.info("co2=%d ch2o=%d tvoc=%d pm25=%d pm10=%d temp=%.1f humi=%.1f",
                     result["co2"], result["ch2o"], result["tvoc"],
                     result["pm25"], result["pm10"],
                     result["temp"], result["humi"])

        except serial.SerialException as e:
            g_up.set(0)
            log.error("串口异常: %s, 重连...", e)
            try: ser.close()
            except: pass
            time.sleep(2)
            ser = open_serial()
        except Exception as e:
            g_rxerr.inc()
            log.exception("未预期异常: %s", e)
            time.sleep(1)

if __name__ == "__main__":
    main()
