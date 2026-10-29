#!/usr/bin/env python3
# ============================================================
# air-sensor exporter: 读 CH340 串口 -> 解析 17字节帧 -> Prometheus
# 帧格式: 3C 02 [CO2_H CO2_L] [CH2O_H CH2O_L] [TVOC_H TVOC_L]
#              [PM25_H PM25_L] [PM10_H PM10_L] [T_int T_dec]
#              [H_int H_dec] [checksum]
# ============================================================
import serial
import time
import logging
from prometheus_client import Gauge, start_http_server

# ---------- 配置 ----------
SERIAL_PORT = "/dev/ttyUSB0"
BAUD = 9600
METRICS_PORT = 8000

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
g_rxerr = Gauge("air_rx_error_total", "Frame checksum/parse error count")

# ---------- 帧解析 ----------
def parse_frame(buf: bytes):
    """buf 长度必须 = 17, 返回 dict 或 None(校验失败)"""
    if len(buf) != 17 or buf[0] != 0x3C or buf[1] != 0x02:
        return None
    if sum(buf[0:16]) & 0xFF != buf[16]:
        return None

    co2   = (buf[2]  << 8) | buf[3]
    ch2o  = (buf[4]  << 8) | buf[5]
    tvoc  = (buf[6]  << 8) | buf[7]
    pm25  = (buf[8]  << 8) | buf[9]
    pm10  = (buf[10] << 8) | buf[11]

    t_int = buf[12] & 0x7F
    t_dec = buf[13]
    temp  = t_int + t_dec / 10.0
    if buf[12] & 0x80:
        temp = -temp

    humi  = buf[14] + buf[15] / 10.0

    return dict(co2=co2, ch2o=ch2o, tvoc=tvoc,
                pm25=pm25, pm10=pm10,
                temp=temp, humi=humi)

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
    buf = bytearray()

    while True:
        try:
            chunk = ser.read(64)
            if not chunk:
                continue
            buf.extend(chunk)

            # 在缓冲区里找帧头 0x3C
            while len(buf) >= 17:
                # 找 0x3C
                idx = buf.find(b"\x3c")
                if idx < 0:
                    buf.clear()
                    break
                if idx > 0:
                    del buf[:idx]
                    continue
                # buf[0] == 0x3C
                frame = bytes(buf[:17])
                result = parse_frame(frame)
                if result is None:
                    g_rxerr.inc()
                    # 丢 1 字节继续找下一个帧头
                    del buf[0]
                    continue
                # 解析成功 -> 更新指标
                g_co2.set(result["co2"])
                g_ch2o.set(result["ch2o"])
                g_tvoc.set(result["tvoc"])
                g_pm25.set(result["pm25"])
                g_pm10.set(result["pm10"])
                g_temp.set(result["temp"])
                g_humi.set(result["humi"])
                g_up.set(1)
                log.info("co2=%d ch2o=%d tvoc=%d pm25=%d pm10=%d temp=%.1f humi=%.1f",
                         result["co2"], result["ch2o"], result["tvoc"],
                         result["pm25"], result["pm10"],
                         result["temp"], result["humi"])
                del buf[:17]

        except serial.SerialException as e:
            g_up.set(0)
            log.error("串口异常: %s, 重连...", e)
            try: ser.close()
            except: pass
            time.sleep(2)
            ser = open_serial()
            buf.clear()
        except Exception as e:
            log.exception("未预期异常: %s", e)
            time.sleep(1)

if __name__ == "__main__":
    main()
