#include "kinematics.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

double clamp_angle(double angle)
{
    return std::clamp(angle, -90.0, 90.0);
}
} // namespace

Position forward_kinematics(const Joint& joint)
{
    const double theta1 = joint.joint1 * kDegToRad;
    const double theta2 = joint.joint2 * kDegToRad;
    const double theta3 = joint.joint3 * kDegToRad;

    // 平面投影径向长度 R 与高度 H。
    const double r = kA2 * std::sin(theta2)
                     + kA3 * std::sin(theta2 + theta3);
    const double h = kA1 + kA2 * std::cos(theta2)
                     + kA3 * std::cos(theta2 + theta3);

    Position position;
    position.x = r * std::cos(theta1);
    position.y = r * std::sin(theta1);
    position.z = h;
    return position;
}

bool inverse_kinematics(const Position& target,
                        Joint& solution_1,
                        Joint& solution_2)
{
    const double r = std::hypot(target.x, target.y);
    const double h = target.z - kA1;

    // 几何法解三角形：
    // a = 2hA2, b = 2A2R, c = h^2 + A2^2 + R^2 - A3^2
    const double a = 2.0 * h * kA2;
    const double b = 2.0 * kA2 * r;
    const double c = h * h + kA2 * kA2 + r * r - kA3 * kA3;

    const double discriminant = a * a + b * b - c * c;
    if (discriminant < 0.0)
    {
        return false;
    }

    const double mid = std::sqrt(discriminant);
    const double atan_ab = std::atan2(b, a);

    const double theta2_1 = atan_ab + std::atan2(mid, c);
    const double theta2_2 = atan_ab - std::atan2(mid, c);

    const double theta23_1 =
        std::atan2(r - kA2 * std::sin(theta2_1), h - kA2 * std::cos(theta2_1));
    const double theta23_2 =
        std::atan2(r - kA2 * std::sin(theta2_2), h - kA2 * std::cos(theta2_2));

    const double theta3_1 = theta23_1 - theta2_1;
    const double theta3_2 = theta23_2 - theta2_2;
    const double theta1 = std::atan2(target.y, target.x);

    solution_1.joint1 = theta1 * kRadToDeg;
    solution_1.joint2 = theta2_1 * kRadToDeg;
    solution_1.joint3 = theta3_1 * kRadToDeg;

    solution_2.joint1 = theta1 * kRadToDeg;
    solution_2.joint2 = theta2_2 * kRadToDeg;
    solution_2.joint3 = theta3_2 * kRadToDeg;

    return true;
}

Pulse angle_to_pulse(const Joint& joint)
{
    const double j1 = clamp_angle(joint.joint1);
    const double j2 = clamp_angle(joint.joint2);
    const double j3 = clamp_angle(joint.joint3);

    Pulse pulse;
    pulse.pulse1 = static_cast<int>((kPulse1Max - kPulse1Min) * (j1 + 90.0) / 180.0
                                    + kPulse1Min);
    pulse.pulse2 = static_cast<int>((kPulse2Max - kPulse2Min) * (j2 + 90.0) / 180.0
                                    + kPulse2Min);
    pulse.pulse3 = static_cast<int>((kPulse3Max - kPulse3Min) * (j3 + 90.0) / 180.0
                                    + kPulse3Min);
    return pulse;
}
