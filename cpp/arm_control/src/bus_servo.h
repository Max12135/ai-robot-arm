#pragma once

#include <cstdint>
#include "serial_port.h"

// 德晟/亚博总线舵机：11 字节写位置帧，8 字节读位置帧。
// 位置范围 96~4000，时间单位 ms。
void bus_servo_move(SerialPort& port, uint8_t id, int position, int time_ms);

// 读取当前位置；成功返回 true，position 为 96~4000。
bool bus_servo_read_position(SerialPort& port, uint8_t id,
                             uint16_t& position, DWORD timeout_ms = 200);
