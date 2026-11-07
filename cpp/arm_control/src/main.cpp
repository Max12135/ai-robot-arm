#include <cstdlib>
#include <iostream>
#include <string>

#include <windows.h>

#include "arm_control.h"
#include "bus_servo.h"
#include "kinematics.h"
#include "serial_port.h"

namespace
{
constexpr uint8_t kServoIds[] = {1, 2, 3, 5, 6};

void read_all(SerialPort& port)
{
    for (uint8_t id : kServoIds)
    {
        uint16_t position = 0;
        if (bus_servo_read_position(port, id, position))
        {
            std::cout << "ID " << static_cast<int>(id)
                      << " position = " << position << '\n';
        }
        else
        {
            std::cout << "ID " << static_cast<int>(id) << " read failed\n";
        }
        Sleep(50);
    }
}

void print_joint(const Joint& joint)
{
    std::cout << joint.joint1 << ", " << joint.joint2 << ", "
              << joint.joint3 << " deg";
}

void print_pulse(const Pulse& pulse)
{
    std::cout << "Pulse: " << pulse.pulse1 << ", " << pulse.pulse2 << ", "
              << pulse.pulse3;
}

// 老师的验证流程（PPT 第 30 页点 1）：给角度 -> 正解 -> 再逆解，
// 看能否回到同一组角度，顺便观察中间变量。
void run_kinematics_demo(double j1, double j2, double j3)
{
    const Joint input{j1, j2, j3};
    std::cout << "Input joint angles: ";
    print_joint(input);
    std::cout << '\n';

    const Position position = forward_kinematics(input);
    std::cout << "Forward: x=" << position.x << " mm, y=" << position.y
              << " mm, z=" << position.z << " mm\n";

    Joint solution_1;
    Joint solution_2;
    if (!inverse_kinematics(position, solution_1, solution_2))
    {
        std::cout << "Inverse: target out of workspace\n";
        return;
    }

    std::cout << "Inverse solution 1: ";
    print_joint(solution_1);
    std::cout << '\n';
    std::cout << "Inverse solution 2: ";
    print_joint(solution_2);
    std::cout << '\n';

    const Pulse pulse = angle_to_pulse(input);
    std::cout << "Pulse for input angles: ";
    print_pulse(pulse);
    std::cout << '\n';
}

// 给定基座坐标 (x,y,z)，完整走一遍“逆解 -> 选解 -> 换算脉冲”，不碰串口。
void run_ik_demo(double x, double y, double z)
{
    const Position target{x, y, z};
    std::cout << "Target: x=" << target.x << " mm, y=" << target.y
              << " mm, z=" << target.z << " mm\n";

    Joint solution_1;
    Joint solution_2;
    if (!inverse_kinematics(target, solution_1, solution_2))
    {
        std::cout << "Inverse: target out of workspace\n";
        return;
    }

    std::cout << "Inverse solution 1: ";
    print_joint(solution_1);
    std::cout << '\n';
    std::cout << "Inverse solution 2: ";
    print_joint(solution_2);
    std::cout << '\n';

    Joint chosen;
    Pulse pulse;
    if (!plan_position(target, chosen, pulse))
    {
        std::cout << "No solution fits the +-90 deg joint range\n";
        return;
    }

    std::cout << "Chosen joint angles: ";
    print_joint(chosen);
    std::cout << '\n';
    std::cout << "Pulse: ";
    print_pulse(pulse);
    std::cout << '\n';
}

// 老师的示范流程（《四轴机械臂控制过程实现》第 4 页）：
// 点 1 -> 抓取 -> 上抬 -> 点 2 -> 松开 -> 回中。
void run_demo(SerialPort& port)
{
    std::cout << "[1] move to point 1 (45, 45, 90)\n";
    move_joint(port, Joint{45.0, 45.0, 90.0}, 1500);
    Sleep(1700);

    std::cout << "[2] grab\n";
    set_gripper(port, true);
    Sleep(900);

    std::cout << "[3] lift (45, 30, 60)\n";
    move_joint(port, Joint{45.0, 30.0, 60.0}, 1200);
    Sleep(1400);

    std::cout << "[4] move to point 2 (0, 30, 60)\n";
    move_joint(port, Joint{0.0, 30.0, 60.0}, 1200);
    Sleep(1400);

    std::cout << "[5] release\n";
    set_gripper(port, false);
    Sleep(900);

    std::cout << "[6] reback to home\n";
    go_home(port);
    Sleep(1200);
}

void print_usage()
{
    std::cout <<
        "Usage:\n"
        "  arm_control kin [j1 j2 j3]        angles -> FK -> IK (no serial)\n"
        "  arm_control ik [x y z]            position -> IK -> pulse (no serial)\n"
        "  arm_control <COM> read            read IDs 1/2/3/5/6\n"
        "  arm_control <COM> mid             go to upright mid position\n"
        "  arm_control <COM> goto <x> <y> <z> [time_ms]\n"
        "  arm_control <COM> move <id> <position> [time_ms]\n"
        "  arm_control <COM> grab | release\n"
        "  arm_control <COM> roll <pulse> [time_ms]\n"
        "  arm_control <COM> demo            teacher's point1-grab-lift-point2 demo\n";
}
} // namespace

int main(int argc, char** argv)
{
    std::string port_name = "COM5";
    int arg_index = 1;

    const std::string first = (argc > 1) ? argv[1] : "";
    if (argc > 1 && first != "kin" && first != "ik")
    {
        port_name = argv[1];
        arg_index = 2;
    }

    const std::string mode = (argc > arg_index) ? argv[arg_index] : "read";

    // 纯运动学验证不需要碰串口，先单独处理。
    if (mode == "kin")
    {
        const double j1 = (argc > arg_index + 1) ? std::atof(argv[arg_index + 1]) : 45.0;
        const double j2 = (argc > arg_index + 2) ? std::atof(argv[arg_index + 2]) : 30.0;
        const double j3 = (argc > arg_index + 3) ? std::atof(argv[arg_index + 3]) : 60.0;
        run_kinematics_demo(j1, j2, j3);
        return 0;
    }
    if (mode == "ik")
    {
        const double x = (argc > arg_index + 1) ? std::atof(argv[arg_index + 1]) : 135.0;
        const double y = (argc > arg_index + 2) ? std::atof(argv[arg_index + 2]) : 135.0;
        const double z = (argc > arg_index + 3) ? std::atof(argv[arg_index + 3]) : 179.0;
        run_ik_demo(x, y, z);
        return 0;
    }

    SerialPort port;
    const std::wstring wide_port(port_name.begin(), port_name.end());
    if (!port.open(wide_port))
    {
        std::cerr << "Failed to open " << port_name
                  << ", error = " << GetLastError() << '\n';
        return 1;
    }

    std::cout << "Opened " << port_name << " @ 115200 8N1\n";

    if (mode == "read")
    {
        read_all(port);
    }
    else if (mode == "mid" || mode == "home")
    {
        go_home(port);
        Sleep(1200);
        read_all(port);
    }
    else if (mode == "goto")
    {
        if (argc < arg_index + 4)
        {
            std::cerr << "Usage: arm_control <COM> goto <x> <y> <z> [time_ms]\n";
            return 2;
        }

        const Position target{std::atof(argv[arg_index + 1]),
                              std::atof(argv[arg_index + 2]),
                              std::atof(argv[arg_index + 3])};
        const int time_ms =
            (argc > arg_index + 4) ? std::stoi(argv[arg_index + 4]) : 1200;

        Joint joint;
        Pulse pulse;
        if (!plan_position(target, joint, pulse))
        {
            std::cerr << "Target outside workspace or joint range\n";
            return 2;
        }

        std::cout << "Chosen joint angles: ";
        print_joint(joint);
        std::cout << '\n';
        std::cout << "Pulse: ";
        print_pulse(pulse);
        std::cout << '\n';

        move_joint(port, joint, time_ms);
        std::cout << "Sent in " << time_ms << " ms\n";
    }
    else if (mode == "move")
    {
        if (argc < arg_index + 3)
        {
            std::cerr << "Usage: arm_control <COM> move <id> <position> [time_ms]\n";
            return 2;
        }

        const int id = std::stoi(argv[arg_index + 1]);
        const int position = std::stoi(argv[arg_index + 2]);
        const int time_ms = (argc > arg_index + 3) ? std::stoi(argv[arg_index + 3]) : 800;

        bus_servo_move(port, static_cast<uint8_t>(id), position, time_ms);
        std::cout << "Sent ID " << id << " -> " << position
                  << " in " << time_ms << " ms\n";
    }
    else if (mode == "grab")
    {
        set_gripper(port, true);
        std::cout << "Gripper closed (" << kGripperClosedPulse << ")\n";
    }
    else if (mode == "release")
    {
        set_gripper(port, false);
        std::cout << "Gripper open (" << kGripperOpenPulse << ")\n";
    }
    else if (mode == "roll")
    {
        if (argc < arg_index + 2)
        {
            std::cerr << "Usage: arm_control <COM> roll <pulse> [time_ms]\n";
            return 2;
        }

        const int pulse = std::stoi(argv[arg_index + 1]);
        const int time_ms = (argc > arg_index + 2) ? std::stoi(argv[arg_index + 2]) : 800;

        set_roll(port, pulse, time_ms);
        std::cout << "Roll (ID 5) -> " << pulse << " in " << time_ms << " ms\n";
    }
    else if (mode == "demo")
    {
        run_demo(port);
    }
    else
    {
        std::cerr << "Unknown mode: " << mode << '\n';
        print_usage();
        return 2;
    }

    return 0;
}
