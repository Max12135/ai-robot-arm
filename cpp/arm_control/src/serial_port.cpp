#include "serial_port.h"

SerialPort::~SerialPort()
{
    close();
}

bool SerialPort::open(const std::wstring& port_name, DWORD baud_rate)
{
    close();

    const std::wstring path = L"\\\\.\\" + port_name;
    handle_ = CreateFileW(path.c_str(),
                          GENERIC_READ | GENERIC_WRITE,
                          0,
                          nullptr,
                          OPEN_EXISTING,
                          0,
                          nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(handle_, &dcb))
    {
        close();
        return false;
    }

    dcb.BaudRate = baud_rate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(handle_, &dcb))
    {
        close();
        return false;
    }

    SetupComm(handle_, 4096, 4096);

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 100;
    if (!SetCommTimeouts(handle_, &timeouts))
    {
        close();
        return false;
    }

    flush_input();
    return true;
}

void SerialPort::close()
{
    if (handle_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool SerialPort::write_bytes(const uint8_t* data, size_t length)
{
    if (!is_open() || data == nullptr || length == 0)
    {
        return false;
    }

    DWORD written = 0;
    return WriteFile(handle_, data, static_cast<DWORD>(length), &written, nullptr)
           && written == length;
}

int SerialPort::read_bytes(uint8_t* buffer, size_t length, DWORD timeout_ms)
{
    if (!is_open() || buffer == nullptr || length == 0)
    {
        return 0;
    }

    int total = 0;
    const DWORD start = GetTickCount();

    while (static_cast<size_t>(total) < length)
    {
        const DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeout_ms)
        {
            break;
        }

        DWORD received = 0;
        if (!ReadFile(handle_,
                      buffer + total,
                      static_cast<DWORD>(length - total),
                      &received,
                      nullptr))
        {
            break;
        }
        total += static_cast<int>(received);
    }

    return total;
}

void SerialPort::flush_input()
{
    if (is_open())
    {
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
    }
}
