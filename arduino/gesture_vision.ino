#include <SoftwareSerial.h>
#define RX_MAX_BUF 8
// 软串口配置：接 ASRPRO 语音模块   RX=2, TX=3
SoftwareSerial softSerial(2, 3);

// 第二路软串口：接 USB-TTL（AI 视觉模块）  RX=D4, TX=D5，波特率 9600
SoftwareSerial visionSerial(4, 5);
const uint32_t VISION_BAUD = 9600;

// ---- 风扇（继电器）----
const int FAN_PIN = A0;      // 继电器 IN
const int FAN_ON  = HIGH;    // 实测为高电平触发
const int FAN_OFF = LOW;

// ---- 风扇运行状态 ----
bool fanOn = false;              // 风扇当前是否通电
bool fanTimed = false;           // 定时吹风模式
bool fanIntermittent = false;    // 间歇吹风模式
bool fanOscillate = false;       // 摇头模式（风扇 + 1 号舵机）
uint64_t fanTimerStart = 0;      // 定时模式开始时刻
uint64_t fanToggleTime = 0;      // 间歇模式上次切换时刻

const uint64_t TIMED_FAN_MS = 10000;    // 定时吹风 10 秒
const uint64_t INTERMITTENT_MS = 5000;  // 间歇吹风 5 秒切换

//接收数据相关变量
uint8_t Rx_Data[8] = {0};
uint8_t Rx_index = 0;
uint8_t Rx_Flag = 0;
uint8_t RecvFlag = 0;

// 舵机定时运行变量
uint64_t time_run  = 0;      // 1 号舵机上次发命令的时刻
uint64_t time_run2 = 0;      // 2 号舵机上次发命令的时刻

// 语音控制：1/0 管 1 号，3/2 管 2 号
bool servo1Run = false;      // 1 号舵机开关
bool servo2Run = false;      // 2 号舵机开关
String receivedString = "";  // 存放本次收到的语音指令


/* 控制总线舵机，
 * id：要控制的id号，0xfe为全体控制
 * value：位置值（96~4000） 
 * time：运行的时间，时间越小，运行越快，最小为0 
 * */
void bus_servo_control(int id, int value, int time)
{
    uint8_t head1 = 0xff;
    uint8_t head2 = 0xff;
    uint8_t s_id = id & 0xff;
    uint8_t len = 0x07;
    uint8_t cmd = 0x03;
    uint8_t addr = 0x2a;

    if (value > 4000)
        value = 4000;
    else if (value < 96)
        value = 96;

    uint8_t pos_H = (value >> 8) & 0xff;
    uint8_t pos_L = value & 0xff;

    uint8_t time_H = (time >> 8) & 0xff;
    uint8_t time_L = time & 0xff;

    uint8_t checknum = (~(s_id + len + cmd + addr +
                          pos_H + pos_L + time_H + time_L)) & 0xff;
    uint8_t data[] = {head1, head2, s_id, len, cmd,
                      addr, pos_H, pos_L, time_H, time_L, checknum};

    Serial.write(data, 11);
}

/* 写入目标ID(1~250) */
void bus_servo_set_id(uint8_t id)
{
    if ((id >= 1) && (id <= 250))
    {
        uint8_t head1 = 0xff;
        uint8_t head2 = 0xff;
        uint8_t s_id = 0xfe; /* 发送广播的ID */
        uint8_t len = 0x04;
        uint8_t cmd = 0x03;
        uint8_t addr = 0x05;
        uint8_t set_id = id; /* 实际写入的ID */

        uint8_t checknum = (~(s_id + len + cmd + addr + set_id)) & 0xff;
        uint8_t data[] = {head1, head2, s_id, len, cmd, addr, set_id, checknum};

        Serial.write(data, 8);
    }
}

/* 发送读取舵机位置命令 */
void bus_servo_read(uint8_t id)
{
    uint8_t head1 = 0xff;
    uint8_t head2 = 0xff;
    uint8_t s_id = id & 0xff;
    uint8_t len = 0x04;
    uint8_t cmd = 0x02;
    uint8_t param_H = 0x38;
    uint8_t param_L = 0x02;

    uint8_t checknum = (~(s_id + len + cmd + param_H + param_L)) & 0xff;
    uint8_t data[] = {head1, head2, s_id, len, cmd, param_H, param_L, checknum};

    Serial.write(data, 8);
}

//转化接收到的值为位置数
uint16_t bus_servo_get_value(void)
{
    uint8_t checknum = (~(Rx_Data[2] + Rx_Data[3] + Rx_Data[4] + Rx_Data[5] + Rx_Data[6])) & 0xff;
    if(checknum == Rx_Data[7])
    {
        uint8_t s_id = Rx_Data[2];
        uint16_t value_H = 0;
        uint16_t value_L = 0;

        value_H = Rx_Data[5];
        value_L = Rx_Data[6];
        uint16_t value = (value_H << 8) + value_L;
        return value;
    }
    return 0;
}

//处理串口数据，如果符合协议则设置RecvFlag = 1
void bus_servo_uart_recv(uint8_t Rx_Temp)
{
    switch(Rx_Flag)
    {
        case 0:
            if(Rx_Temp == 0xff)
            {
                Rx_Data[0] = 0xff;
                Rx_Flag = 1;
            }
            else if (Rx_Temp == 0xf5)
            {
                Rx_Data[0] = 0xff;
                Rx_Data[1] = 0xf5;
                Rx_Flag = 2;
                Rx_index = 2;
            }
            break;

        case 1:
            if(Rx_Temp == 0xf5)
            {
                Rx_Data[1] = 0xf5;
                Rx_Flag = 2;
                Rx_index = 2;
            } else
            {
                Rx_Flag = 0;
                Rx_Data[0] = 0x0;
            }
            break;

        case 2:
            Rx_Data[Rx_index] = Rx_Temp;
            Rx_index++;

            if(Rx_index >= RX_MAX_BUF)
            {
                Rx_Flag = 0;
                RecvFlag = 1;
            }
            break;

        default:
            break;
    }
}

// 风扇开关：统一处理继电器触发电平
void fan_set(bool on)
{
    fanOn = on;
    digitalWrite(FAN_PIN, on ? FAN_ON : FAN_OFF);
}

// 退出所有风扇特殊模式（定时 / 间歇 / 摇头）
void fan_stop_modes(void)
{
    fanTimed = false;
    fanIntermittent = false;
    fanOscillate = false;
}

// 手势接管 1、2 号舵机前，先停掉语音驱动的来回摆动和摇头，
// 否则 1 号舵机每 3 秒会被语音模式重新拉走，和手势姿势打架。
void gesture_take_control(void)
{
    servo1Run = false;
    servo2Run = false;
    fanOscillate = false;
}

void setup()
{
    Serial.begin(115200);
    softSerial.begin(115200);   // 与 ASRPRO 端波特率一致
    softSerial.setTimeout(50);  // 缩短读超时，减少两路软串口互相阻塞

    visionSerial.begin(VISION_BAUD);  // 与 PC 端 PySerial 一致
    visionSerial.setTimeout(50);
    softSerial.listen();              // 默认先监听语音模块

    // 风扇默认关闭
    pinMode(FAN_PIN, OUTPUT);
    fan_set(false);

  //  bus_servo_set_id(1);
    delay(10);
}

void loop()
{
    // ---- 0. 两路软串口轮流监听：语音(D2/D3) 150ms，视觉(D4/D5) 50ms ----
    // Arduino 的 SoftwareSerial 同一时刻只能收一路，所以按时间片切换。
    // PC 端每条命令会重复发送多次，保证至少有一次落在视觉监听窗口里。
    static uint32_t lastListenSwitch = 0;
    static bool visionListening = false;
    uint32_t listenWindow = visionListening ? 50 : 150;
    if (millis() - lastListenSwitch >= listenWindow)
    {
        lastListenSwitch = millis();
        visionListening = !visionListening;
        if (visionListening)
            visionSerial.listen();
        else
            softSerial.listen();
    }

    // ---- 0a. AI 视觉模块指令（USB-TTL -> D4/D5，9600）----
    if (visionListening && visionSerial.available() > 0)
    {
        String visionCmd = visionSerial.readStringUntil('\n');
        visionCmd.trim();

        // 手势命令：三个手势分别启动三个不同的执行器，区分最直观
        //   rock    -> 1 号舵机
        //   paper   -> 2 号舵机
        //   scissors-> 风扇
        // reback：两个舵机回中位、风扇关闭。
        if (visionCmd == "rock")
        {
            gesture_take_control();
            fan_stop_modes();
            fan_set(false);
            bus_servo_control(0x01, 800, 1000);    // 1 号舵机转到 800
            bus_servo_control(0x02, 2048, 1000);   // 2 号舵机保持中位
            Serial.println("VISION_ROCK");
        }
        else if (visionCmd == "paper")
        {
            gesture_take_control();
            fan_stop_modes();
            fan_set(false);
            bus_servo_control(0x01, 2048, 1000);   // 1 号舵机保持中位
            bus_servo_control(0x02, 3100, 1000);   // 2 号舵机转到 3100
            Serial.println("VISION_PAPER");
        }
        else if (visionCmd == "scissors")
        {
            gesture_take_control();
            fan_stop_modes();
            bus_servo_control(0x01, 2048, 1000);   // 两个舵机回中位
            bus_servo_control(0x02, 2048, 1000);
            fan_set(true);                         // 风扇启动
            Serial.println("VISION_SCISSORS");
        }
        else if (visionCmd == "reback")
        {
            gesture_take_control();
            fan_stop_modes();
            fan_set(false);                        // 风扇关闭
            bus_servo_control(0x01, 2048, 1000);   // 两个舵机回中位
            bus_servo_control(0x02, 2048, 1000);
            Serial.println("VISION_REBACK");
        }
    }

    // ---- 1. 接收语音模块指令（ASRPRO -> D2/D3，115200）----
    if (!visionListening && softSerial.available() > 0)
    {
        // 读取字符串直到换行符或超时
        receivedString = softSerial.readStringUntil('\n');
        // 移除字符串末尾的回车符和换行符
        receivedString.trim();
        // 调试信息：打印接收到的字符串
        if (receivedString == "1")
        {
            servo1Run = true;
            Serial.println("SERVO1_START");   // 1 号舵机启动指令
            delay(10);
        }
        else if (receivedString == "0")
        {
            servo1Run = false;
            Serial.println("SERVO1_STOP");    // 1 号舵机停止指令
            delay(10);
        }
        else if (receivedString == "3")
        {
            servo2Run = true;
            Serial.println("SERVO2_START");   // 2 号舵机启动指令
            delay(10);
        }
        else if (receivedString == "2")
        {
            servo2Run = false;
            Serial.println("SERVO2_STOP");    // 2 号舵机停止指令
            delay(10);
        }
        else if (receivedString == "hello")
        {
            Serial.println("WAKE");           // 唤醒词，不控制外设
        }
        else if (receivedString == "4")
        {
            fan_stop_modes();
            fan_set(true);
            Serial.println("FAN_ON");         // 打开风扇
        }
        else if (receivedString == "5")
        {
            fan_stop_modes();
            fan_set(false);
            Serial.println("FAN_OFF");        // 关闭风扇
        }
        else if (receivedString == "6")
        {
            fan_stop_modes();
            fanOscillate = true;
            fan_set(true);
            Serial.println("FAN_OSCILLATE");  // 风扇摇头：风扇 + 1 号舵机
        }
        else if (receivedString == "7")
        {
            fanOscillate = false;             // 只停舵机，风扇继续转
            Serial.println("FAN_OSCILLATE_STOP"); // 停止摇头
        }
        else if (receivedString == "8")
        {
            fan_stop_modes();
            fanTimed = true;
            fanTimerStart = millis();
            fan_set(true);
            Serial.println("FAN_TIMED");      // 定时吹风 10 秒
        }
        else if (receivedString == "9")
        {
            fan_stop_modes();
            fanIntermittent = true;
            fanToggleTime = millis();
            fan_set(true);
            Serial.println("FAN_INTERMITTENT"); // 间歇吹风
        }
    }

    // ---- 2. 收到"1"后 1 号舵机来回摆动；收到"0"就停；摇头模式也会带动它 ----
    if ((servo1Run || fanOscillate) && (millis() - time_run >= 3000))
    {
        static int state = 1;
        if (state)
        {
            bus_servo_control(0x01, 800, 2000);    // 慢慢转到 900
            state = 0;
        }
        else
        {
            bus_servo_control(0x01, 3000, 2000);   // 慢慢转到 3100
            state = 1;
        }
        time_run = millis();
    }

    // ---- 3. 收到"3"后 2 号舵机来回摆动；收到"2"就停 ----
    if (servo2Run && (millis() - time_run2 >= 3000))
    {
        static int state2 = 1;
        if (state2)
        {
            bus_servo_control(0x02, 900, 2000);    // 慢慢转到 900
            state2 = 0;
        }
        else
        {
            bus_servo_control(0x02, 3100, 2000);   // 慢慢转到 3100
            state2 = 1;
        }
        time_run2 = millis();
    }

    // ---- 4. 定时吹风：到 10 秒自动关 ----
    if (fanTimed && (millis() - fanTimerStart >= TIMED_FAN_MS))
    {
        fanTimed = false;
        fan_set(false);
        Serial.println("FAN_TIMEOUT");
    }

    // ---- 5. 间歇吹风：每 5 秒切换一次 ----
    if (fanIntermittent && (millis() - fanToggleTime >= INTERMITTENT_MS))
    {
        fanToggleTime = millis();
        fan_set(!fanOn);
        Serial.println(fanOn ? "FAN_ON" : "FAN_OFF");
    }

    // ---- 6. 位置回读（原有逻辑保留；本任务不发读命令，所以不会触发）----
    if (RecvFlag)
    {
        uint16_t value = bus_servo_get_value();
        if (value)
        {
            Serial.print("\r\nvalue=");
            Serial.println(value);
        }
        else
        {
            Serial.println("\r\nread error");
        }
        RecvFlag = 0;
    }
}

//串口中断
void serialEvent()
{
    while (Serial.available())
    {
        uint8_t RXTemp = (uint8_t)Serial.read();
        bus_servo_uart_recv(RXTemp);
        // Serial.write(&RXTemp, 1);
    }
}