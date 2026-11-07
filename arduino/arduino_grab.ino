/**********
author: songjinhua
date:20260416
针对某一构型的四自由度机械臂正逆运动学解算与封装

串口命令（9600，一行一条，\n 结尾）：
    gesture+a1+a2+a3 / 目标名+a1+a2+a3    三个关节角（度）
    grab / release / reback               夹紧 / 松开 / 回中位
    x,y,z                                 坐标抓取（原功能，mm）
    read5                                  读 5 号舵机当前位置（调姿态用）
    wrist+脉冲                             直接把 5 号舵机设到指定脉冲（96~4000）
    fan / fanoff                           风扇开 / 关（继电器，A0 高电平触发）
    show                                   夹爪张开-夹紧-张开-夹紧-张开
***************************/

# include<SoftwareSerial.h>
// 定义软件串口引脚，用于语音模块/视觉模块
SoftwareSerial voiceSerial(2, 3); // RX, TX
#define RX_MAX_BUF 8

#define CONF   0.01745//角度弧度的转换
#define PI   3.14159

// ===== 坐标抓取参数（可按实机调整）=====
#define SERIAL_BAUD      9600    //坐标输入口波特率，要和 USB-TTL 端一致
#define APPROACH_MM      50.0f   //先到目标点上方多少毫米
#define TOOL_OFFSET_Z    0.0f    //夹爪长度补偿：正值表示腕部再抬高
#define MOVE_TIME        1200    //水平移动时间 ms
#define DESCEND_TIME     700     //下探时间 ms
#define GRIP_OPEN        1700    //夹爪张开位置
#define GRIP_CLOSE       2900    //夹爪闭合位置
#define GRIP_TIME        900     //夹爪动作时间 ms
#define PULSE_MARGIN     0       //行程余量（老师示例刚好用到 J3=3150）
#define Z_MIN_MM         20.0f   //最低安全高度
#define R_MIN_MM         20.0f   //离底座最近距离
#define CMD_BUF_SIZE     40      //一行命令最大长度
#define DRY_RUN          0       //1=只回脉冲不动作；验证完改成 0
#define ANGLE_MOVE_TIME  2000    //老师角度指令：每轴运行时间 ms
#define REBACK_TIME      2000    //回中位的运行时间 ms
#define WRIST_STRAIGHT   200     //5号舵机（腕部旋转）调正位置，实测：200 时夹爪朝正

// ===== 风扇（继电器）=====
#define FAN_PIN          A0      //继电器 IN
#define FAN_ON           HIGH    //实测高电平触发
#define FAN_OFF          LOW
#define SHOW_STEP_TIME   450     //夹爪展示每次动作时间 ms
#define SHOW_STEP_WAIT   600     //夹爪展示每次动作后的等待 ms

/**定义结构体表示1，2，3轴旋转角，角度制；
4轴控制抓取姿态，不参与逆解，直接指定**/
struct  Joint
{
    float joint1;//角度制
    float joint2;
    float joint3;
};
struct Position
{
    float x;//固定坐标系位置，mm
    float y;
    float z;
};

struct Pulse
{
    int pulse1;//舵机脉冲位置
    int pulse2;
    int pulse3;
};
//机械臂连杆长度参数
float length_A1 = 108;//mm
float length_A2 = 82.5;
float length_A3 = 150;
float R, H;//分别是平面投影径向长度，和垂直面投影的高度

// 舵机定时运行变量
uint64_t time_run = 0;

//arduino接收数据相关变量
uint8_t Rx_Data[8] = {0};
uint8_t Rx_index = 0;
uint8_t Rx_Flag = 0;
uint8_t RecvFlag = 0;


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
    uint8_t cmd = 0x03;//写指令
    uint8_t addr = 0x2a;//首地址

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

void setup()
{
    Serial.begin(115200);//设置硬件串口，接舵机总线
    delay(10);
    voiceSerial.begin(SERIAL_BAUD);//坐标输入口 D2/D3，接 USB-TTL
    voiceSerial.println("READY");
    bus_servo_control(0x05, WRIST_STRAIGHT, 1500);//上电把腕部旋转摆正
    pinMode(FAN_PIN, OUTPUT);//风扇默认关闭
    digitalWrite(FAN_PIN, FAN_OFF);
}

/***传入参数是角度，输出是X,Y,Z坐标值，用一个结构体一次性传出***/
Position Forward_Kinematics( Joint arm)
{
    Position pos;
    float seta1 = CONF * arm.joint1;//转为弧度制，后续三角函数参数一律弧度计算
    float seta2 = CONF * arm.joint2;
    float seta3 = CONF * arm.joint3;

    R = length_A2 * sin(seta2) + length_A3 * sin(seta2 + seta3);
    H = length_A1 + length_A2 * cos(seta2) + length_A3 * cos(seta2 + seta3);
   
    pos.x = R * cos(seta1);
    pos.y = R * sin(seta1);
    pos.z = H;
 //   std::cout << "pos.x: " << pos.x << "    ;" << "pos.y: " << pos.y << "    ;" << "pos.z: " << pos.z << "\n";

    return pos;
}
 Joint Inverse_Kinematics(Position pos)
{
     Joint joint_1, joint_2;//共两组解
     float a, b, c;
     R = sqrt(pos.x*pos.x + pos.y*pos.y);
     float h;
     h = pos.z - length_A1;

     a = 2 * h * length_A2;
     b = 2 * length_A2 * R;
     c = h * h + length_A2 * length_A2 + R * R - length_A3 * length_A3;

     float seta1 = atan2f(pos.y, pos.x);

     float seta2_1, seta2_2;//两组seta2
     float mid_data = sqrt(a*a + b*b -c*c);
     seta2_1 = atan2f(b, a) + atan2f(mid_data, c);
     seta2_2 = atan2f(b, a) - atan2f(mid_data, c);

     float seta23_1, seta23_2;//两组seta23
     seta23_1 = atan2f(R - length_A2 * sin(seta2_1), h - length_A2 * cos(seta2_1));
     seta23_2 = atan2f(R - length_A2 * sin(seta2_2), h - length_A2 * cos(seta2_2));
    
     float seta3_1, seta3_2;//两组seta3
     seta3_1 = seta23_1 - seta2_1;
     seta3_2 = seta23_2 - seta2_2;

     joint_1.joint1 = seta1 * 180/PI;
     joint_1.joint2 = seta2_1 * 180 / PI;
     joint_1.joint3 = seta3_1 * 180 / PI;

     joint_2.joint1 = seta1 * 180 / PI;
     joint_2.joint2 = seta2_2 * 180 / PI;
     joint_2.joint3 = seta3_2 * 180 / PI;

   //  std::cout << " joint_1.joint1: " << joint_1.joint1 << "    ;" << " joint_1.joint2: " << joint_1.joint2 << "    ;" << " joint_1.joint3: " << joint_1.joint3 << "\n";
   //  std::cout << " joint_2.joint1: " << joint_2.joint1 << "    ;" << " joint_2.joint2: " << joint_2.joint2 << "    ;" << " joint_2.joint3: " << joint_2.joint3 << "\n";

     return joint_2;//取其中一组返回
}
 //只写了前三轴的脉冲位置，第四轴单独发脉冲，不参与逆解；传入参数是前三轴角度值
 //手动在机械臂实体校准过各个轴，得到的转换关系
 Pulse  Angle_to_pulse(Joint arm)
 {
     Pulse pulse;
     pulse.pulse1 = (3050 - 850) * (arm.joint1 + 90) / 180 + 850;
     pulse.pulse2 = (3100 - 950) * (arm.joint2 + 90) / 180 + 950;
     pulse.pulse3 = (3150 - 950) * (arm.joint3 + 90) / 180 + 950;
   //  std::cout << "pulse.pulse1: " << pulse.pulse1 << "    ;" << " pulse.pulse2: " << pulse.pulse2 << "    ;" << "pulse.pulse3: " << pulse.pulse3 << "\n";
     return pulse;
 }

// ================= 坐标抓取功能 =================
//坐标输入缓冲
char cmd_buf[CMD_BUF_SIZE];
uint8_t cmd_len = 0;

//脉冲是否在标定行程内
bool pulses_in_range(const Pulse &p)
{
    return p.pulse1 >= 850 + PULSE_MARGIN && p.pulse1 <= 3050 - PULSE_MARGIN &&
           p.pulse2 >= 950 + PULSE_MARGIN && p.pulse2 <= 3100 - PULSE_MARGIN &&
           p.pulse3 >= 950 + PULSE_MARGIN && p.pulse3 <= 3150 - PULSE_MARGIN;
}

/* 带检查的反解：目标不可达或超行程返回 false；
 * 两组解里优先选 θ2>0 的，其次 |θ3| 小的 */
bool solve_ik(Position pos, Joint &out)
{
    float r = sqrt(pos.x * pos.x + pos.y * pos.y);
    if (r < R_MIN_MM || pos.z < Z_MIN_MM)
        return false;

    float h = pos.z - length_A1;
    float a = 2 * h * length_A2;
    float b = 2 * length_A2 * r;
    float c = h * h + length_A2 * length_A2 + r * r - length_A3 * length_A3;

    float disc = a * a + b * b - c * c;
    if (disc < 0)
        return false;
    float mid_data = sqrt(disc);

    float seta1 = atan2f(pos.y, pos.x);
    float seta2[2], seta3[2];
    seta2[0] = atan2f(b, a) + atan2f(mid_data, c);
    seta2[1] = atan2f(b, a) - atan2f(mid_data, c);

    for (int i = 0; i < 2; i++)
    {
        float seta23 = atan2f(r - length_A2 * sinf(seta2[i]),
                              h - length_A2 * cosf(seta2[i]));
        seta3[i] = seta23 - seta2[i];
    }

    int best = -1;
    float best_score = 1e9f;
    for (int i = 0; i < 2; i++)
    {
        Joint cand;
        cand.joint1 = seta1 * 180 / PI;
        cand.joint2 = seta2[i] * 180 / PI;
        cand.joint3 = seta3[i] * 180 / PI;
        Pulse cand_pulse = Angle_to_pulse(cand);
        if (!pulses_in_range(cand_pulse))
            continue;
        float score = (cand.joint2 > 0 ? 0 : 1000) + fabsf(cand.joint3);
        if (score < best_score)
        {
            best_score = score;
            best = i;
        }
    }
    if (best < 0)
        return false;

    out.joint1 = seta1 * 180 / PI;
    out.joint2 = seta2[best] * 180 / PI;
    out.joint3 = seta3[best] * 180 / PI;
    return true;
}

//角度解 -> 脉冲，并做行程检查
bool solve_and_convert(Position pos, Pulse &pulse)
{
    Joint joint;
    if (!solve_ik(pos, joint))
        return false;
    pulse = Angle_to_pulse(joint);
    return pulses_in_range(pulse);
}

//三轴下发（错开 10ms，实际近似同时启动）
void move_pulses(const Pulse &p, int run_time)
{
    bus_servo_control(0x01, p.pulse1, run_time);
    delay(10);
    bus_servo_control(0x02, p.pulse2, run_time);
    delay(10);
    bus_servo_control(0x03, p.pulse3, run_time);
    delay(10);
}

void reply_pulses(const char *tag, const Pulse &p)
{
    voiceSerial.print(tag);
    voiceSerial.print(' ');
    voiceSerial.print(p.pulse1);
    voiceSerial.print(',');
    voiceSerial.print(p.pulse2);
    voiceSerial.print(',');
    voiceSerial.println(p.pulse3);
}

void drain_serial_input()
{
    while (voiceSerial.available())
        voiceSerial.read();
}

// ================= 老师角度协议 =================
//角度范围与老师示例一致，避免奇异构型
bool is_valid_joint_angles(const Joint &a)
{
    return a.joint1 >= -90 && a.joint1 <= 90 &&
           a.joint2 >= -90 && a.joint2 <= 90 &&
           a.joint3 >= 0 && a.joint3 <= 135;
}

void Close_gripper()
{
    bus_servo_control(0x06, GRIP_CLOSE, GRIP_TIME);
    delay(GRIP_TIME + 50);
}

void Release_gripper()
{
    bus_servo_control(0x06, GRIP_OPEN, 700);
    delay(750);
}

//夹爪展示：张开-夹紧-张开-夹紧-张开
void show_gripper()
{
    bus_servo_control(0x06, GRIP_OPEN, SHOW_STEP_TIME);
    delay(SHOW_STEP_WAIT);
    bus_servo_control(0x06, GRIP_CLOSE, SHOW_STEP_TIME);
    delay(SHOW_STEP_WAIT);
    bus_servo_control(0x06, GRIP_OPEN, SHOW_STEP_TIME);
    delay(SHOW_STEP_WAIT);
    bus_servo_control(0x06, GRIP_CLOSE, SHOW_STEP_TIME);
    delay(SHOW_STEP_WAIT);
    bus_servo_control(0x06, GRIP_OPEN, SHOW_STEP_TIME);
    delay(SHOW_STEP_WAIT);
}

//风扇开关（继电器）
void fan_set(bool on)
{
    digitalWrite(FAN_PIN, on ? FAN_ON : FAN_OFF);
}

void backToZero()//只考虑了1，2，3轴
{
    bus_servo_control(0x01, 2000, REBACK_TIME);
    delay(10);
    bus_servo_control(0x02, 2000, REBACK_TIME);
    delay(10);
    bus_servo_control(0x03, 2000, REBACK_TIME);
    delay(REBACK_TIME + 100);
}

//解析一行 "目标名+a1+a2+a3"（如 gesture+45+45+90），角度制；前缀不校验，只认第一个 '+'
//与老师示例一致：先正解再反解做一次往返校验，然后转脉冲下发
void process_angle_command(char *line)
{
    char *p = strchr(line, '+');//跳过 "目标名+" 前缀
    if (p == NULL)
    {
        voiceSerial.println("ERR:format");
        return;
    }
    p++;
    float angles[3];

    for (uint8_t i = 0; i < 3; i++)
    {
        char *end;
        angles[i] = strtod(p, &end);
        if (end == p)
        {
            voiceSerial.println("ERR:format");
            return;
        }
        p = end;
        if (i < 2)
        {
            if (*p != '+')
            {
                voiceSerial.println("ERR:format");
                return;
            }
            p++;
        }
    }
    while (*p == ' ')
        p++;
    if (*p != '\0')
    {
        voiceSerial.println("ERR:format");
        return;
    }

    Joint arm;
    arm.joint1 = angles[0];
    arm.joint2 = angles[1];
    arm.joint3 = angles[2];

    if (!is_valid_joint_angles(arm))
    {
        voiceSerial.println("ERR:range");
        return;
    }

    Position pos = Forward_Kinematics(arm);
    arm = Inverse_Kinematics(pos);

    Pulse pulse = Angle_to_pulse(arm);
    if (!pulses_in_range(pulse))
    {
        voiceSerial.println("ERR:travel");
        return;
    }

    reply_pulses("OK", pulse);
    move_pulses(pulse, ANGLE_MOVE_TIME);
    delay(ANGLE_MOVE_TIME + 100);
    voiceSerial.println("DONE");
}

//解析一行 "x,y,z"，单位 mm
void process_coordinate_command(char *line)
{
    char *p = line;
    char *end;

    float x = strtod(p, &end);
    if (end == p)
    {
        voiceSerial.println("ERR:format");
        return;
    }
    p = end;
    if (*p != ',')
    {
        voiceSerial.println("ERR:format");
        return;
    }
    p++;
    float y = strtod(p, &end);
    if (end == p)
    {
        voiceSerial.println("ERR:format");
        return;
    }
    p = end;
    if (*p != ',')
    {
        voiceSerial.println("ERR:format");
        return;
    }
    p++;
    float z = strtod(p, &end);
    if (end == p)
    {
        voiceSerial.println("ERR:format");
        return;
    }
    p = end;
    while (*p == ' ')
        p++;
    if (*p != '\0')
    {
        voiceSerial.println("ERR:format");
        return;
    }

    Position target;
    target.x = x;
    target.y = y;
    target.z = z + TOOL_OFFSET_Z;

    Position above = target;
    above.z += APPROACH_MM;

    //先解算两组位置，任意一组不可达就完全不动
    Pulse pulse_above, pulse_target;
    if (!solve_and_convert(target, pulse_target))
    {
        voiceSerial.println("ERR:target");
        return;
    }
    if (!solve_and_convert(above, pulse_above))
    {
        voiceSerial.println("ERR:approach");
        return;
    }

#if DRY_RUN
    reply_pulses("DRY T", pulse_target);//T=目标点
    reply_pulses("DRY A", pulse_above);//A=目标上方
#else
    reply_pulses("OK", pulse_target);

    //动作期间发来的行不再响应，先清掉
    drain_serial_input();

    bus_servo_control(0x06, GRIP_OPEN, 700);//张开爪子
    delay(800);

    move_pulses(pulse_above, MOVE_TIME);//到目标正上方
    delay(MOVE_TIME + 100);

    move_pulses(pulse_target, DESCEND_TIME);//下探
    delay(DESCEND_TIME + 100);

    bus_servo_control(0x06, GRIP_CLOSE, GRIP_TIME);//夹紧
    delay(GRIP_TIME + 100);

    voiceSerial.println("DONE");
    drain_serial_input();
#endif
}

// ================= 5号舵机手动校准 =================
//读 5 号舵机当前位置。不同固件返回长度略有差别，所以原始字节和两种
//可能的解析都打印出来，以与实际角度吻合的那个为准。
void read_servo5()
{
    RecvFlag = 0;
    Rx_Flag = 0;
    Rx_index = 0;
    for (uint8_t i = 0; i < RX_MAX_BUF; i++)
        Rx_Data[i] = 0;

    bus_servo_read(0x05);

    uint32_t start = millis();
    while (!RecvFlag && (millis() - start) < 500)
    {
        while (Serial.available())
        {
            bus_servo_uart_recv((uint8_t)Serial.read());
            if (RecvFlag)
                break;
        }
    }
    if (!RecvFlag)
    {
        voiceSerial.println("ERR:read5");
        return;
    }
    RecvFlag = 0;

    voiceSerial.print("RAW5");
    for (uint8_t i = 0; i < RX_MAX_BUF; i++)
    {
        voiceSerial.print(' ');
        if (Rx_Data[i] < 0x10)
            voiceSerial.print('0');
        voiceSerial.print(Rx_Data[i], HEX);
    }
    voiceSerial.println();

    voiceSerial.print("POS5 ");
    voiceSerial.print(((int)Rx_Data[5] << 8) | Rx_Data[6]);
    voiceSerial.print(" / ");
    voiceSerial.println(((int)Rx_Data[6] << 8) | Rx_Data[7]);
}

//wrist+脉冲：直接把 5 号舵机设到指定脉冲位置，用于调正姿态
void process_wrist_command(char *line)
{
    char *p = strchr(line, '+');
    if (p == NULL)
    {
        voiceSerial.println("ERR:format");
        return;
    }
    p++;
    char *end;
    long value = strtol(p, &end, 10);
    while (*end == ' ')
        end++;
    if (end == p || *end != '\0')
    {
        voiceSerial.println("ERR:format");
        return;
    }
    if (value < 96)
        value = 96;
    if (value > 4000)
        value = 4000;

    bus_servo_control(0x05, (int)value, 1000);
    delay(1050);
    voiceSerial.print("OK5 ");
    voiceSerial.println(value);
}

//一行命令分发：5号舵机校准优先，其次老师角度协议、动作命令，最后坐标模式
void process_line(char *line)
{
    if (strncmp(line, "wrist", 5) == 0)
    {
        process_wrist_command(line);
    }
    else if (strncmp(line, "read5", 5) == 0)
    {
        read_servo5();
    }
    else if (strchr(line, '+') != NULL)
    {
        process_angle_command(line);
    }
    else if (strcmp(line, "grab") == 0)
    {
        Close_gripper();
        voiceSerial.println("DONE");
    }
    else if (strcmp(line, "release") == 0)
    {
        Release_gripper();
        voiceSerial.println("DONE");
    }
    else if (strcmp(line, "reback") == 0)
    {
        backToZero();
        Release_gripper();
        fan_set(false);
        voiceSerial.println("DONE");
    }
    else if (strcmp(line, "fan") == 0)
    {
        fan_set(true);
        voiceSerial.println("DONE");
    }
    else if (strcmp(line, "fanoff") == 0)
    {
        fan_set(false);
        voiceSerial.println("DONE");
    }
    else if (strcmp(line, "show") == 0)
    {
        show_gripper();
        voiceSerial.println("DONE");
    }
    else if (strchr(line, ',') != NULL)
    {
        process_coordinate_command(line);
    }
    else
    {
        voiceSerial.println("ERR:unknown");
    }
}

//非阻塞收行，收到换行就执行
void handle_serial_input()
{
    while (voiceSerial.available())
    {
        char c = (char)voiceSerial.read();
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            cmd_buf[cmd_len] = '\0';
            if (cmd_len > 0)
                process_line(cmd_buf);
            cmd_len = 0;
        }
        else if (cmd_len < CMD_BUF_SIZE - 1)
        {
            cmd_buf[cmd_len++] = c;
        }
        else
        {
            cmd_len = 0;
            voiceSerial.println("ERR:too_long");
        }
    }
}




void loop()
{
    handle_serial_input();//等待电脑发来的角度或坐标命令
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