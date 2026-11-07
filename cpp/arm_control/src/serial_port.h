#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

// 简单的 Windows 串口封装：打开、写、按超时读。
class SerialPort
{
public:
    SerialPort() = default;
    ~SerialPort();

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool open(const std::wstring& port_name, DWORD baud_rate = 115200);
    void close();
    bool is_open() const { return handle_ != INVALID_HANDLE_VALUE; }

    bool write_bytes(const uint8_t* data, size_t length);
    int read_bytes(uint8_t* buffer, size_t length, DWORD timeout_ms);
    void flush_input();

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};
