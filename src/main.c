#include <mcs51/8052.h>

// ========== 模式开关 ==========
// IS_DEBUG = 1 : 输出文字调试信息（用硬件串口打印）
// IS_DEBUG = 0 : 透传模式——收到传感器字节原样转发到TX，供其他设备二次监听；同时LCD照常显示
#define IS_DEBUG 1

#define XTAL_HZ 11059200UL
#define UART_BAUD 9600UL
#define UART_TIMER1_RELOAD ((unsigned char)(256UL - (XTAL_HZ / 12UL / 32UL / UART_BAUD)))
#define SENSOR_FRAME_WAIT_MS 5000U

// ================== UART Functions ==================
void uart_send_byte(unsigned char value)
{
    ES = 0;          // 关串口中断，防止ISR抢清TI导致死等
    SBUF = value;
    while (!TI)
        ;
    TI = 0;
    ES = 1;          // 恢复串口接收中断
}
void uart_print_str(char *s)
{
    while (*s)
    {
        uart_send_byte(*s++);
    }
}

void uart_print_hex8(unsigned char v)
{
    unsigned char h = (v >> 4) & 0x0F;
    unsigned char l = v & 0x0F;
    uart_send_byte((h < 10) ? ('0' + h) : ('A' + h - 10));
    uart_send_byte((l < 10) ? ('0' + l) : ('A' + l - 10));
}

void uart_print_hex16(unsigned int v)
{
    uart_print_hex8((unsigned char)(v >> 8));
    uart_print_hex8((unsigned char)(v & 0xFF));
}
static void uart_init(void)
{
    SCON = 0x50;   // 模式1, 8位UART, REN=1(允许接收)
    TMOD = (TMOD & 0x0F) | 0x20;  // T1模式2 自动重装
    TH1 = UART_TIMER1_RELOAD;
    TL1 = UART_TIMER1_RELOAD;
    TR1 = 1;
    ES = 1;        // 使能串口中断（接收传感器数据用）
}
// ================== UART Functions End ==================

// ================Delay functions Start ==================
void delay_ms(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 120; j++)
            ;
}
// ================Delay functions End ==================

// ================== LCD Functions Start ==================
// 核心新增：字节按位反转
unsigned char reverse_byte(unsigned char dat)
{
    unsigned char res = 0;
    unsigned char i;
    for (i = 0; i < 8; i++)
    {
        res <<= 1;
        res |= (dat & 0x01);
        dat >>= 1;
    }
    return res;
}
void lcd_write(unsigned char is_data, unsigned char val)
{
    P2 |= 0x08;  /* RD=1 (P2.3) */
    P2 &= ~0x01; // CS(P2.0)=0，选中芯片
    if (is_data)
        P2 |= 0x02;
    else
        P2 &= ~0x02; /* RS (P2.1) */
    P2 &= ~0x04;     /* WR=0 (P2.2) */
    P1 = val;
    delay_ms(1);
    P2 |= 0x04; /* WR=1，上升沿锁存 */
    P2 |= 0x01; // CS(P2.0)=1，释放芯片
    delay_ms(1);
}
void lcd_cmd(unsigned char c) { lcd_write(0, c); }
void lcd_dat(unsigned char d) { lcd_write(1, reverse_byte(d)); } // 【改动点】输出数据自动反转bit

void lcd_set_pos(unsigned char page, unsigned char col)
{
    lcd_cmd(0xB0 | (page & 0x0F));
    lcd_cmd(0x10 | ((col >> 4) & 0x0F));
    lcd_cmd(0x00 | (col & 0x0F));
}
void lcd_fill(unsigned char pattern)
{
    unsigned char p, c;
    for (p = 0; p < 8; p++)
    {
        lcd_set_pos(p, 0);
        for (c = 0; c < 128; c++)
            lcd_dat(pattern);
    }
}
//===== 数字 0~9 =====
const unsigned char FONT_0[8] = {0x00,0x3E,0x61,0x51,0x49,0x45,0x3E,0x00};
const unsigned char FONT_1[8] = {0x00,0x00,0x00,0x42,0x7F,0x40,0x00,0x00};
const unsigned char FONT_2[8] = {0x00,0x42,0x61,0x51,0x49,0x46,0x00,0x00};
const unsigned char FONT_3[8] = {0x00,0x22,0x41,0x49,0x49,0x49,0x36,0x00};
const unsigned char FONT_4[8] = {0x00,0x18,0x14,0x12,0x7F,0x10,0x10,0x00};
const unsigned char FONT_5[8] = {0x00,0x27,0x45,0x45,0x45,0x39,0x00,0x00};
const unsigned char FONT_6[8] = {0x3C,0x4A,0x49,0x49,0x49,0x49,0x30,0x00};
const unsigned char FONT_7[8] = {0x00,0x01,0x01,0x71,0x0D,0x03,0x00,0x00};
const unsigned char FONT_8[8] = {0x00,0x36,0x49,0x49,0x49,0x49,0x36,0x00};
const unsigned char FONT_9[8] = {0x00,0x06,0x49,0x49,0x49,0x49,0x3E,0x00};

// //===== 符号 =====
const unsigned char FONT_Point[8]   = {0x00,0x60,0x60,0x00,0x00,0x00,0x00,0x00}; // . 小数点
const unsigned char FONT_Colon[8]   = {0x00,0x00,0x6C,0x00,0x00,0x00,0x00,0x00}; // : 冒号
const unsigned char FONT_Percent[8] ={0x00,0x46,0x26,0x10,0x08,0x64,0x62,0x00}; // % 百分号
const unsigned char FONT_Minus[8]   = {0x00,0x08,0x08,0x08,0x08,0x00,0x00,0x00}; // - 负号
const unsigned char FONT_Space[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // 空格

// //===== 大写字母（CO2 CH2O TVOC PM25 PM10）=====
const unsigned char FONT_C[8] = {0x3E,0x41,0x41,0x41,0x41,0x41,0x22,0x00};
const unsigned char FONT_O[8] = {0x3E,0x41,0x41,0x41,0x41,0x41,0x3E,0x00};
const unsigned char FONT_H[8] = {0x7F,0x08,0x08,0x08,0x08,0x08,0x7F,0x00};
const unsigned char FONT_T[8] = {0x01,0x01,0x01,0x7F,0x01,0x01,0x01,0x00};
const unsigned char FONT_V[8] = {0x07,0x18,0x60,0x80,0x60,0x18,0x07,0x00};
const unsigned char FONT_P[8] = {0x7F,0x11,0x11,0x11,0x11,0x0E,0x00,0x00};
const unsigned char FONT_M[8] = {0x7F,0x02,0x04,0x08,0x04,0x02,0x7F,0x00};
const unsigned char FONT_R[8]  = {0x00,0xFF,0x11,0x31,0x51,0x8E,0x00,0x00}; //大写R %R

// //===== 小写字母（Temp  Humi）=====
const unsigned char FONT_e[8] = {0x00,0x00,0x38,0x54,0x54,0x54,0x18,0x00};
const unsigned char FONT_m[8] = {0x00,0x00,0x78,0x04,0x7C,0x04,0x78,0x00};
const unsigned char FONT_p[8] = {0x00,0x00,0x00,0xFC,0x24,0x24,0x18,0x00};
const unsigned char FONT_u[8] = {0x00,0x00,0x00,0x3C,0x40,0x40,0x7C,0x40};
const unsigned char FONT_g[8] = {0x00,0x00,0x00,0x98,0xA4,0xA4,0x78,0x00};
const unsigned char FONT_i[8] = {0x00,0x00,0x00,0x00,0x7A,0x00,0x00,0x00};
const unsigned char FONT_o[8]  = {0x00,0x00,0x00,0x38,0x44,0x44,0x38,0x00}; //小写o

const unsigned char *get_font_ptr(char c)
{
    // return FONT_Space;
    switch (c)
    {
    case '0':
        return FONT_0;
    case '1':
        return FONT_1;
    case '2':
        return FONT_2;
    case '3':
        return FONT_3;
    case '4':
        return FONT_4;
    case '5':
        return FONT_5;
    case '6':
        return FONT_6;
    case '7':
        return FONT_7;
    case '8':
        return FONT_8;
    case '9':
        return FONT_9;

    case ':':
        return FONT_Colon;
    case '.':
        return FONT_Point;
    case '%':
        return FONT_Percent;
    case '-':
        return FONT_Minus;

    case 'C':
        return FONT_C;
    case 'O':
        return FONT_O;
    case 'H':
        return FONT_H;
    case 'T':
        return FONT_T;
    case 'V':
        return FONT_V;
    case 'P':
        return FONT_P;
    case 'M':
        return FONT_M;
    case 'R':
        return FONT_R;

    case 'e':
        return FONT_e;
    case 'm':
        return FONT_m;
    case 'p':
        return FONT_p;
    case 'u':
        return FONT_u;
    case 'i':
        return FONT_i;
    case 'o':
        return FONT_o;
    case 'g':
        return FONT_g;
    case ' ':
        return FONT_Space;
    default:
        return FONT_Space;
    }
}

// 打印单个8*8字符（占用1个page）
void lcd_put8x8(unsigned char page, unsigned char col, const unsigned char *ch)
{
    unsigned char i;
    lcd_set_pos(page, col);
    for (i = 0; i < 8; i++)
    {
        lcd_dat(ch[i]);
    }
}

// page: 0~7, label[7]:7字符标签, val[4]:4字符数值, unit[3]:3字符单位
void lcd_draw_line(unsigned char page, char *label, char *val, char *unit)
{
    unsigned char x = 0;
    const unsigned char *font;
    // 绘制7字符标签
    for (unsigned char i = 0; i < 7; i++)
    {
        font = get_font_ptr(label[i]);
        lcd_put8x8(page, x, font);
        x += 8;
    }
    // 绘制4字符数值
    for (unsigned char i = 0; i < 4; i++)
    {
        font = get_font_ptr(val[i]);
        lcd_put8x8(page, x, font);
        x += 8;
    }
    // 绘制4字符单位
    for (unsigned char i = 0; i < 4; i++)
    {
        font = get_font_ptr(unit[i]);
        lcd_put8x8(page, x, font);
        x += 8;
    }
}

void lcd_draw_val4(unsigned char page, char *val)
{
    unsigned char x = 56;
    unsigned char i;
    for (i = 0; i < 4; i++)
    {
        lcd_put8x8(page, x, get_font_ptr(val[i]));
        x += 8;
    }
}

void lcd_init(void)
{
    P2 = 0x01;   /* CS=1 初始未选中 */
    P2 |= 0x04;  /* WR=1 */
    P2 |= 0x08;  /* RD=1 */
    P2 &= ~0x10; /* RST=0 (P2.4) */
    delay_ms(50);
    P2 |= 0x10; /* RST=1 */
    delay_ms(50);
    lcd_cmd(0xE2); delay_ms(5);
    lcd_cmd(0xA2);
    lcd_cmd(0xA0);
    lcd_cmd(0xC0); // lcd_cmd(0xC8);
    lcd_cmd(0xA6);
    lcd_cmd(0xA4);
    lcd_cmd(0x24);
    lcd_cmd(0x2F); delay_ms(5);
    lcd_cmd(0xF8);
    lcd_cmd(0x01); delay_ms(5);
    lcd_cmd(0x81);
    lcd_cmd(0x38); delay_ms(5);
    lcd_cmd(0x40);
    lcd_cmd(0xAF); delay_ms(5);
    lcd_fill(0x00);
}

void fmt_u4(unsigned int x, char *buf)
{
    unsigned char i, started = 0;
    unsigned int d = 1000;
    if (x > 9999) x = 9999;
    for (i = 0; i < 4; i++)
    {
        unsigned char v = (unsigned char)(x / d);
        x = x % d;
        if (v || started || d == 1)
        {
            buf[i] = (char)('0' + v);
            started = 1;
        }
        else
        {
            buf[i] = ' ';
        }
        d /= 10;
    }
    buf[4] = 0;
}

void fmt_u10_4(unsigned int x10, char *buf)
{
    unsigned char i;
    unsigned int n;

    if (x10 >= 1000)
    {
        buf[0] = '1';
        buf[1] = '0';
        buf[2] = '0';
        buf[3] = '.';
        buf[4] = 0;
        return;
    }

    n = x10 / 10;
    for (i = 0; i < 3; i++)
        buf[i] = ' ';
    i = 1;   // ★修复：个位写buf[1]，buf[2]留给小数点
    do
    {
        buf[i] = (char)('0' + (n % 10));
        n /= 10;
        if (i == 0)
            break;
        i--;
    } while (n > 0);

    buf[2] = '.';
    buf[3] = (char)('0' + (x10 % 10));
    buf[4] = 0;
}

void fmt_s10_4(signed int x10, char *buf)
{
    unsigned int a;
    if (x10 >= 0)
    {
        fmt_u10_4((unsigned int)x10, buf);
        return;
    }

    a = (unsigned int)(-x10);
    if (a >= 100)
    {
        unsigned int n = a / 10;
        buf[0] = '-';
        buf[1] = (char)('0' + (n / 10));
        buf[2] = (char)('0' + (n % 10));
        buf[3] = ' ';
        buf[4] = 0;
    }
    else
    {
        buf[0] = '-';
        buf[1] = (char)('0' + (a / 10));
        buf[2] = '.';
        buf[3] = (char)('0' + (a % 10));
        buf[4] = 0;
    }
}
// ================== LCD Functions End ==================

// ================== Sensor Data (UART) Start ==================

typedef struct
{
    unsigned int co2;
    unsigned int ch2o;
    unsigned int tvoc;
    unsigned int pm25;
    unsigned int pm10;
    signed int temp10;
    unsigned int humi10;
} SensorData;

enum sensor_parse_result
{
    SENSOR_PARSE_NOTREADY = 0,
    SENSOR_PARSE_READY = 1,
    SENSOR_PARSE_REJECT1 = 2,
    SENSOR_PARSE_REJECT2 = 3,
};

static unsigned char frm[17];
static volatile unsigned char idx = 0;
enum sensor_frame_state
{
    SENSOR_FRAME_IDLE = 0,
    SENSOR_FRAME_RECVING,
    SENSOR_FRAME_READY
};
static volatile enum sensor_frame_state sensor_frame_status = SENSOR_FRAME_IDLE;
static SensorData sensor_latest;
static SensorData sensor_work;

enum sensor_parse_result sensor_parse_byte(unsigned char b, SensorData *d)
{
    // 0x3C 是帧头，必须在空闲状态下才接收
    if (idx == 0)
    {
        if (b == 0x3C)
        {
            frm[0] = b;
            idx = 1;
            return SENSOR_PARSE_NOTREADY;
        }
        return SENSOR_PARSE_NOTREADY;
    }

    if (idx == 1)
    {
        if (b != 0x02)
        {
            idx = 0;
            return SENSOR_PARSE_REJECT1;
        }
        frm[1] = b;
        idx = 2;
        return SENSOR_PARSE_NOTREADY;
    }

    // 后续 14 个数据字节：允许出现 0x3C, 不做错误重置
    frm[idx++] = b;
    if (idx < 17)
        return SENSOR_PARSE_NOTREADY;

    // 收满 17 字节，开始校验：B3..B16 求和，取低 8 位，与 B17 比较
    unsigned char i, sum = 0;
    idx = 0;
    sum = 0;
    for (i = 0; i < 16; i++)
        sum += frm[i];
    if ((sum & 0xFFu) != frm[16])
    {
        return SENSOR_PARSE_REJECT2;
    }

    unsigned char t_int, t_dec;
    unsigned int t10;
    d->co2 = ((unsigned int)frm[2] << 8) | frm[3];
    d->ch2o = ((unsigned int)frm[4] << 8) | frm[5];
    d->tvoc = ((unsigned int)frm[6] << 8) | frm[7];
    d->pm25 = ((unsigned int)frm[8] << 8) | frm[9];
    d->pm10 = ((unsigned int)frm[10] << 8) | frm[11];
    t_int = frm[12] & 0x7F;
    t_dec = frm[13];
    t10 = (unsigned int)t_int * 10u + (unsigned int)t_dec;
    d->temp10 = (frm[12] & 0x80) ? -(signed int)t10 : (signed int)t10;
    d->humi10 = (unsigned int)frm[14] * 10u + (unsigned int)frm[15];
    return SENSOR_PARSE_READY;
}

// 新增：专门重置帧解析状态，替代原来传NULL方案
void sensor_parse_reset(void)
{
    idx = 0;
    for (int i = 0; i < 17; i++) frm[i] = 0;
}

void uart_isr(void) __interrupt(4)
{
    unsigned char b;
    if (RI)
    {
        RI = 0;
        b = SBUF;

#if !IS_DEBUG
        // 透传模式：收到的字节立刻从TX发出去，供其他设备监听
        SBUF = b;
#endif

        // 无论调试还是透传，都喂给帧解析（LCD照常工作）
        switch (sensor_parse_byte(b, &sensor_work))
        {
        case SENSOR_PARSE_READY:
            sensor_latest.co2   = sensor_work.co2;
            sensor_latest.ch2o  = sensor_work.ch2o;
            sensor_latest.tvoc  = sensor_work.tvoc;
            sensor_latest.pm25  = sensor_work.pm25;
            sensor_latest.pm10  = sensor_work.pm10;
            sensor_latest.temp10 = sensor_work.temp10;
            sensor_latest.humi10 = sensor_work.humi10;
            sensor_frame_status = SENSOR_FRAME_READY;
            break;
        case SENSOR_PARSE_REJECT1:
        case SENSOR_PARSE_REJECT2:
        case SENSOR_PARSE_NOTREADY:
            break;
        }
    }
    if (TI)
    {
        TI = 0;
    }
}

// ================== Sensor Data (UART) End ==================

// ================== Main Function ==================

void main(void)
{
    SensorData s;
    char v_humi[5] = "----";
    char v_temp[5] = "----";
    char v_co2[5]  = "----";
    char v_pm10[5] = "----";
    char v_pm25[5] = "----";
    char v_tvoc[5] = "----";
    char v_ch2o[5] = "----";
    unsigned char frame_ok;
    unsigned int wait_ms;

    uart_init();

#if IS_DEBUG
    uart_print_str("=== HW UART RX debug mode ===\r\n");
#endif

    lcd_init();
#if IS_DEBUG
    uart_print_str("LCD initialized.\r\n");
#endif

    lcd_draw_line(6, "Humi:  ", v_humi, " %RH");
    lcd_draw_line(5, "Temp:  ", v_temp, " oC ");
    lcd_draw_line(4, "CO2 :  ", v_co2, " ppm");
    lcd_draw_line(3, "PM10:  ", v_pm10, " ug ");
    lcd_draw_line(2, "PM25:  ", v_pm25, " ug ");
    lcd_draw_line(1, "TVOC:  ", v_tvoc, " ug ");
    lcd_draw_line(0, "CH2O:  ", v_ch2o, " ug ");

#if IS_DEBUG
    uart_print_str("Init done. Waiting sensor frame...\r\n");
#endif

    EA = 1;   // 开总中断，硬件串口RX中断才能工作

    while (1)
    {
#if IS_DEBUG
        uart_print_str("==== Start capture ====\r\n");
#endif
        EA = 0;
        sensor_parse_reset();
        sensor_frame_status = SENSOR_FRAME_IDLE;

        EA = 1;
        frame_ok = 0;
        wait_ms = 0;
        
        while (wait_ms < SENSOR_FRAME_WAIT_MS)
        {
            if (sensor_frame_status == SENSOR_FRAME_READY)
            {
                EA = 0;
                s.co2   = sensor_latest.co2;
                s.ch2o  = sensor_latest.ch2o;
                s.tvoc  = sensor_latest.tvoc;
                s.pm25  = sensor_latest.pm25;
                s.pm10  = sensor_latest.pm10;
                s.temp10 = sensor_latest.temp10;
                s.humi10 = sensor_latest.humi10;
                sensor_frame_status = SENSOR_FRAME_IDLE;
                EA = 1;

                frame_ok = 1;
                fmt_u10_4(s.humi10, v_humi);
                fmt_s10_4(s.temp10, v_temp);
                fmt_u4(s.co2, v_co2);
                fmt_u4(s.pm10, v_pm10);
                fmt_u4(s.pm25, v_pm25);
                fmt_u4(s.tvoc, v_tvoc);
                fmt_u4(s.ch2o, v_ch2o);

                lcd_draw_val4(6, v_humi);
                lcd_draw_val4(5, v_temp);
                lcd_draw_val4(4, v_co2);
                lcd_draw_val4(3, v_pm10);
                lcd_draw_val4(2, v_pm25);
                lcd_draw_val4(1, v_tvoc);
                lcd_draw_val4(0, v_ch2o);

#if IS_DEBUG
                uart_print_str("OK!");
                uart_print_str("frm bytes:");
                for (unsigned char i = 0; i < 17; i++)
                {
                    uart_send_byte(' ');
                    uart_print_hex8(frm[i]);
                }

                uart_print_str("\r\n");
                uart_print_str(" ch2o=");
                uart_print_str(v_ch2o);
                uart_print_str(" tvoc=");
                uart_print_str(v_tvoc);
                uart_print_str(" pm25=");
                uart_print_str(v_pm25);
                uart_print_str(" pm10=");
                uart_print_str(v_pm10);
                uart_print_str(" co2=");
                uart_print_str(v_co2);
                uart_print_str(" temp=");
                uart_print_str(v_temp);
                uart_print_str(" humi=");
                uart_print_str(v_humi);
                uart_print_str("\r\n");
#endif
                break;
            }
            delay_ms(1);
            wait_ms++;
        }

        if (!frame_ok)
        {
#if IS_DEBUG
            uart_print_str("TIMEOUT\r\n");
#endif
            sensor_parse_reset();
        }
    }
}

// End of main.c