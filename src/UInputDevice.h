#pragma once

#include <QString>

class UInputDevice
{
public:
    UInputDevice();
    ~UInputDevice();

    bool openDevice(QString* error = nullptr, int absMaxX = 0, int absMaxY = 0);
    void closeDevice();
    bool isOpen() const;

    bool emitEvent(int type, int code, int value, QString* error = nullptr);
    bool emitRelativeMove(int dx, int dy, QString* error = nullptr);
    bool emitWheel(int dx, int dy, bool hires, QString* error = nullptr);
    bool emitAbsolutePosition(int x, int y, QString* error = nullptr);
    bool sync(QString* error = nullptr);

private:
    bool writeEvent(int type, int code, int value, QString* error);
    int m_fd = -1;
    bool m_hasAbsolute = false;
    int m_absMaxX = 0;
    int m_absMaxY = 0;
};
