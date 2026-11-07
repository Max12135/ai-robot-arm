#pragma once

#include "kinematics.h"
#include "serial_port.h"

// 直立中位/夹爪脉冲，取自老师 PPT 第 20、24 页：
// J1/J2/J3 用 0° 对应的脉冲；ID5(roll) 中位 1450；
// ID6 开口 5.5cm 对应 1700，闭合 2900。
constexpr int kRollMidPulse = 1450;
constexpr int kGripperOpenPulse = 1700;
constexpr int kGripperClosedPulse = 2900;

// 老师的流程：目标点 P(x,y,z) -> 逆解 -> 选一组可用解 -> 换算脉冲。
// 纯计算，不碰串口；供 VS 里调试和 ik 模式验证用。
bool plan_position(const Position& target, Joint& joint, Pulse& pulse);

// 把前三轴角度换算成脉冲，依次发给 ID 1/2/3。
void move_joint(SerialPort& port, const Joint& joint, int time_ms = 1000);

// 第四轴 roll 只管抓取姿态，不参与逆解，单独按脉冲控制。
void set_roll(SerialPort& port, int pulse, int time_ms = 800);

// 6 号舵机：闭合/张开。
void set_gripper(SerialPort& port, bool closed, int time_ms = 800);

// 回直立中位：J1/J2/J3 = 0°，ID5 = 1450，ID6 = 1700。
void go_home(SerialPort& port, int time_ms = 1000);
