#include "UInputDevice.h"

#include <QtGlobal>

#include <cerrno>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>

UInputDevice::UInputDevice() = default;

UInputDevice::~UInputDevice()
{
    closeDevice();
}

bool UInputDevice::openDevice(QString* error, int absMaxX, int absMaxY)
{
    if (m_fd >= 0) return true;

    m_fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        if (error) *error = QString("Could not open /dev/uinput: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }

    auto setBit = [&](unsigned long request, int bit, const char* label) -> bool {
        if (::ioctl(m_fd, request, bit) < 0) {
            if (error) *error = QString("uinput setup failed for %1=%2: %3").arg(label).arg(bit).arg(QString::fromLocal8Bit(std::strerror(errno)));
            return false;
        }
        return true;
    };

    if (!setBit(UI_SET_EVBIT, EV_KEY, "EV_KEY")) return false;
    if (!setBit(UI_SET_EVBIT, EV_REL, "EV_REL")) return false;
    if (!setBit(UI_SET_EVBIT, EV_SYN, "EV_SYN")) return false;

    Q_UNUSED(absMaxX);
    Q_UNUSED(absMaxY);
    // Keep the virtual device as a simple relative mouse + keyboard.
    // Mixing ABS_X/ABS_Y with REL_X/REL_Y caused some Wayland/libinput setups
    // to classify the device poorly and ignore movement. Absolute movement is
    // handled in MacroPlayer with relative steps when cursor feedback is live.
    m_hasAbsolute = false;
    m_absMaxX = 0;
    m_absMaxY = 0;

#ifdef UI_SET_PROPBIT
#ifdef INPUT_PROP_POINTER
    // Help libinput/KWin classify this uinput device as a pointer device.
    ::ioctl(m_fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
#endif
#endif

    for (int code = 0; code <= KEY_MAX; ++code) {
        ::ioctl(m_fd, UI_SET_KEYBIT, code);
    }

    // Be explicit about common mouse buttons, even though they are within KEY_MAX.
    ::ioctl(m_fd, UI_SET_KEYBIT, BTN_LEFT);
    ::ioctl(m_fd, UI_SET_KEYBIT, BTN_RIGHT);
    ::ioctl(m_fd, UI_SET_KEYBIT, BTN_MIDDLE);
    ::ioctl(m_fd, UI_SET_KEYBIT, BTN_SIDE);
    ::ioctl(m_fd, UI_SET_KEYBIT, BTN_EXTRA);
#ifdef BTN_TOOL_MOUSE
    ::ioctl(m_fd, UI_SET_KEYBIT, BTN_TOOL_MOUSE);
#endif

    if (!setBit(UI_SET_RELBIT, REL_X, "REL_X")) return false;
    if (!setBit(UI_SET_RELBIT, REL_Y, "REL_Y")) return false;
    if (!setBit(UI_SET_RELBIT, REL_WHEEL, "REL_WHEEL")) return false;
    if (!setBit(UI_SET_RELBIT, REL_HWHEEL, "REL_HWHEEL")) return false;
#ifdef REL_WHEEL_HI_RES
    ::ioctl(m_fd, UI_SET_RELBIT, REL_WHEEL_HI_RES);
#endif
#ifdef REL_HWHEEL_HI_RES
    ::ioctl(m_fd, UI_SET_RELBIT, REL_HWHEEL_HI_RES);
#endif

    uinput_setup setup {};
    std::snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "Task Automation Virtual Input");
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1209;
    setup.id.product = 0x4d52;
    setup.id.version = 1;

    if (::ioctl(m_fd, UI_DEV_SETUP, &setup) < 0) {
        if (error) *error = QString("UI_DEV_SETUP failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        closeDevice();
        return false;
    }
    if (::ioctl(m_fd, UI_DEV_CREATE) < 0) {
        if (error) *error = QString("UI_DEV_CREATE failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        closeDevice();
        return false;
    }

    ::usleep(300000);
    if (m_hasAbsolute) {
#ifdef BTN_TOOL_MOUSE
        writeEvent(EV_KEY, BTN_TOOL_MOUSE, 1, nullptr);
        sync(nullptr);
#endif
    }
    return true;
}

void UInputDevice::closeDevice()
{
    if (m_fd >= 0) {
        ::ioctl(m_fd, UI_DEV_DESTROY);
        ::close(m_fd);
        m_fd = -1;
        m_hasAbsolute = false;
        m_absMaxX = 0;
        m_absMaxY = 0;
    }
}

bool UInputDevice::isOpen() const
{
    return m_fd >= 0;
}

bool UInputDevice::writeEvent(int type, int code, int value, QString* error)
{
    if (m_fd < 0) {
        if (error) *error = "uinput device is not open";
        return false;
    }

    input_event ev {};
    ev.type = type;
    ev.code = code;
    ev.value = value;

    const ssize_t n = ::write(m_fd, &ev, sizeof(ev));
    if (n != sizeof(ev)) {
        if (error) *error = QString("uinput write failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    return true;
}

bool UInputDevice::emitEvent(int type, int code, int value, QString* error)
{
    if (!writeEvent(type, code, value, error)) return false;
    return sync(error);
}

bool UInputDevice::emitRelativeMove(int dx, int dy, QString* error)
{
    if (dx != 0 && !writeEvent(EV_REL, REL_X, dx, error)) return false;
    if (dy != 0 && !writeEvent(EV_REL, REL_Y, dy, error)) return false;
    return sync(error);
}


bool UInputDevice::emitWheel(int dx, int dy, bool hires, QString* error)
{
    auto standardDetentsFromHiRes = [](int value) -> int {
        if (value == 0) return 0;
        // Linux high-resolution wheel units are normally 120 units per detent.
        // Only emit a legacy detent when the high-resolution value reaches at
        // least one full detent; otherwise let the compositor/app handle the
        // high-resolution scroll amount alone.
        if (value >= 120 || value <= -120) return value / 120;
        return 0;
    };

    if (hires) {
#ifdef REL_HWHEEL_HI_RES
        if (dx != 0 && !writeEvent(EV_REL, REL_HWHEEL_HI_RES, dx, error)) return false;
#else
        Q_UNUSED(dx);
#endif
#ifdef REL_WHEEL_HI_RES
        if (dy != 0 && !writeEvent(EV_REL, REL_WHEEL_HI_RES, dy, error)) return false;
#else
        Q_UNUSED(dy);
#endif
        const int legacyDx = standardDetentsFromHiRes(dx);
        const int legacyDy = standardDetentsFromHiRes(dy);
        if (legacyDx != 0 && !writeEvent(EV_REL, REL_HWHEEL, legacyDx, error)) return false;
        if (legacyDy != 0 && !writeEvent(EV_REL, REL_WHEEL, legacyDy, error)) return false;
    } else {
        if (dx != 0 && !writeEvent(EV_REL, REL_HWHEEL, dx, error)) return false;
        if (dy != 0 && !writeEvent(EV_REL, REL_WHEEL, dy, error)) return false;
    }

    return sync(error);
}


bool UInputDevice::emitAbsolutePosition(int x, int y, QString* error)
{
    if (!m_hasAbsolute) {
        if (error) *error = "uinput absolute positioning is not enabled";
        return false;
    }

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > m_absMaxX) x = m_absMaxX;
    if (y > m_absMaxY) y = m_absMaxY;

    if (!writeEvent(EV_ABS, ABS_X, x, error)) return false;
    if (!writeEvent(EV_ABS, ABS_Y, y, error)) return false;
    return sync(error);
}

bool UInputDevice::sync(QString* error)
{
    return writeEvent(EV_SYN, SYN_REPORT, 0, error);
}
