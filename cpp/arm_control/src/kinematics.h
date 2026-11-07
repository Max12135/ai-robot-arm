#pragma once

// 四轴机械臂运动学（前三轴定位，第 5 号舵机控制抓取 roll，6 号控制夹爪）。
// 角度统一使用“度”，内部计算再转弧度。
struct Joint
{
    double joint1 = 0.0; // 底座 yaw
    double joint2 = 0.0; // 肩 pitch
    double joint3 = 0.0; // 肘 pitch
};

struct Position
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Pulse
{
    int pulse1 = 0;
    int pulse2 = 0;
    int pulse3 = 0;
};

// 杆长，单位 mm（老师 PPT 第 25 页 / ConsoleApplication_4dof_arm.cpp）。
constexpr double kA1 = 108.0;  // 底座到肩关节
constexpr double kA2 = 82.5;   // 大臂
constexpr double kA3 = 150.0;  // 小臂到腕点

Position forward_kinematics(const Joint& joint);

// 几何逆解，可能有两组解；返回 false 表示目标超出工作空间。
bool inverse_kinematics(const Position& target,
                        Joint& solution_1,
                        Joint& solution_2);

// 角度 -> 舵机脉冲。端点取自老师代码（“手动在机械臂实体校准过各个轴”）：
// J1: 850~3050 对应 -90°~+90°
// J2: 950~3100 对应 -90°~+90°
// J3: 950~3150 对应 -90°~+90°
// PPT 第 19 页的 J1 示例是 950~3050（0°=2000），与老师代码差 50 脉冲；
// 换机械臂/重新标定后只改下面这 6 个常量即可。
constexpr int kPulse1Min = 850;
constexpr int kPulse1Max = 3050;
constexpr int kPulse2Min = 950;
constexpr int kPulse2Max = 3100;
constexpr int kPulse3Min = 950;
constexpr int kPulse3Max = 3150;

Pulse angle_to_pulse(const Joint& joint);
