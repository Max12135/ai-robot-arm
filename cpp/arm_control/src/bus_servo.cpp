#include "bus_servo.h"

#include <algorithm>
#include <windows.h>

namespace
{
uint8_t checksum(const uint8_t* data, size_t begin, size_t end)
{
    uint8_t sum = 0;
    for (size_t i = begin; i < end; ++i)
    {
        sum = static_cast<uint8_t>(sum + data[i]);
    }
    return static_cast<uint8_t>(~sum);
}
} // namespace

void bus_servo_move(SerialPort& port, uint8_t id, int position, int time_ms)
{
    position = std::clamp(position, 96, 4000);
    if (time_ms < 0)
    {
        time_ms = 0;
    }

    uint8_t frame[11] = {};
    frame[0] = 0xFF;
    frame[1] = 0xFF;
    frame[2] = id;
    frame[3] = 0x07;
    frame[4] = 0x03;                    // 写命令
    frame[5] = 0x2A;                    // 目标位置寄存器
    frame[6] = static_cast<uint8_t>((position >> 8) & 0xFF);
    frame[7] = static_cast<uint8_t>(position & 0xFF);
    frame[8] = static_cast<uint8_t>((time_ms >> 8) & 0xFF);
    frame[9] = static_cast<uint8_t>(time_ms & 0xFF);
    frame[10] = checksum(frame, 2, 10);

    port.write_bytes(frame, sizeof(frame));
}

bool bus_servo_read_position(SerialPort& port, uint8_t id,
                             uint16_t& position, DWORD timeout_ms)
{
    uint8_t request[8] = {};
    request[0] = 0xFF;
    request[1] = 0xFF;
    request[2] = id;
    request[3] = 0x04;
    request[4] = 0x02;                  // 读命令
    request[5] = 0x38;                  // 当前位置寄存器
    request[6] = 0x02;                  // 读 2 字节
    request[7] = checksum(request, 2, 7);

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        port.flush_input();
        if (!port.write_bytes(request, sizeof(request)))
        {
            return false;
        }

        Sleep(20);

        uint8_t response[8] = {};
        if (port.read_bytes(response, sizeof(response), timeout_ms) != sizeof(response))
        {
            continue;
        }

        // 正常应答头为 FF F5；部分固件回 FF FF，这里都接受。
        const bool header_ok = response[0] == 0xFF
                               && (response[1] == 0xF5 || response[1] == 0xFF);
        if (!header_ok || response[2] != id)
        {
            continue;
        }
        if (checksum(response, 2, 7) != response[7])
        {
            continue;
        }

        position = static_cast<uint16_t>((response[5] << 8) | response[6]);
        if (position >= 96 && position <= 4000)
        {
            return true;
        }
    }

    return false;
}
