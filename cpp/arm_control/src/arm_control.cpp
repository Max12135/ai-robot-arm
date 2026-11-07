#include "arm_control.h"

#include <windows.h>

#include "bus_servo.h"

namespace
{
// 脉冲映射只覆盖 ±90°，超出这个范围的角度发出去会被截断。
bool in_range(const Joint& joint)
{
    return joint.joint1 >= -90.0 && joint.joint1 <= 90.0
           && joint.joint2 >= -90.0 && joint.joint2 <= 90.0
           && joint.joint3 >= -90.0 && joint.joint3 <= 90.0;
}

// 逆解有两组。老师建议 θ3 取正，末端落在合理工作空间内；
// 优先选可用且 θ3 ≥ 0 的那组，两组都不在范围内则返回 false。
bool choose_solution(const Joint& first, const Joint& second, Joint& chosen)
{
    if (in_range(first) && first.joint3 >= 0.0)
    {
        chosen = first;
        return true;
    }
    if (in_range(second) && second.joint3 >= 0.0)
    {
        chosen = second;
        return true;
    }
    if (in_range(first))
    {
        chosen = first;
        return true;
    }
    if (in_range(second))
    {
        chosen = second;
        return true;
    }
    return false;
}
} // namespace

bool plan_position(const Position& target, Joint& joint, Pulse& pulse)
{
    Joint solution_1;
    Joint solution_2;
    if (!inverse_kinematics(target, solution_1, solution_2))
    {
        return false;
    }
    if (!choose_solution(solution_1, solution_2, joint))
    {
        return false;
    }

    pulse = angle_to_pulse(joint);
    return true;
}

void move_joint(SerialPort& port, const Joint& joint, int time_ms)
{
    const Pulse pulse = angle_to_pulse(joint);

    // 总线是半双工，一帧一帧发，帧间留一点间隔。
    bus_servo_move(port, 1, pulse.pulse1, time_ms);
    Sleep(20);
    bus_servo_move(port, 2, pulse.pulse2, time_ms);
    Sleep(20);
    bus_servo_move(port, 3, pulse.pulse3, time_ms);
    Sleep(20);
}

void set_roll(SerialPort& port, int pulse, int time_ms)
{
    bus_servo_move(port, 5, pulse, time_ms);
    Sleep(20);
}

void set_gripper(SerialPort& port, bool closed, int time_ms)
{
    bus_servo_move(port, 6,
                   closed ? kGripperClosedPulse : kGripperOpenPulse,
                   time_ms);
    Sleep(20);
}

void go_home(SerialPort& port, int time_ms)
{
    move_joint(port, Joint{}, time_ms);
    Sleep(100);
    set_roll(port, kRollMidPulse);
    Sleep(100);
    set_gripper(port, false);
}
