#include "EvdevRecorder.h"
#include "KWinCursorBridge.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFileInfoList>
#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>

EvdevRecorder::EvdevRecorder(QObject* parent) : QThread(parent) {}

EvdevRecorder::~EvdevRecorder()
{
    requestStop();
    wait(2000);
}

void EvdevRecorder::setCompressMouseMoves(bool)
{
    // No-op. The recorder now stores absolute cursor positions at trigger points.
}

void EvdevRecorder::setCursorBridge(KWinCursorBridge* bridge)
{
    m_cursorBridge = bridge;
}

void EvdevRecorder::requestStop(double trimStopClickSeconds)
{
    if (trimStopClickSeconds < 0.0) trimStopClickSeconds = 0.0;
    m_trimStopClickSeconds.store(trimStopClickSeconds, std::memory_order_relaxed);
    m_stop.storeRelaxed(true);
}

MacroEvents EvdevRecorder::events() const
{
    QMutexLocker locker(&m_mutex);
    return m_events;
}

QVector<EvdevRecorder::OpenDevice> EvdevRecorder::openInputDevices(QString* error)
{
    QVector<OpenDevice> devices;
    QDir dir("/dev/input");
    QFileInfoList entries = dir.entryInfoList(QStringList() << "event*", QDir::Files | QDir::System, QDir::Name);

    for (const QFileInfo& info : entries) {
        const QString path = info.absoluteFilePath();
        int fd = ::open(path.toUtf8().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }

        char name[256] = {0};
        if (::ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
            std::strncpy(name, "unknown", sizeof(name) - 1);
        }

        OpenDevice dev;
        dev.fd = fd;
        dev.path = path;
        dev.name = QString::fromUtf8(name);
        devices.push_back(dev);
    }

    if (devices.isEmpty()) {
        if (error) {
            *error = "Could not open any /dev/input/event* device. Run as your normal user after installing the development udev rule, or run with suitable input permissions.";
        }
    }

    return devices;
}

void EvdevRecorder::closeInputDevices(QVector<OpenDevice>& devices)
{
    for (OpenDevice& dev : devices) {
        if (dev.fd >= 0) {
            ::close(dev.fd);
            dev.fd = -1;
        }
    }
}

void EvdevRecorder::run()
{
    m_stop.storeRelaxed(false);
    m_trimStopClickSeconds.store(0.0, std::memory_order_relaxed);
    {
        QMutexLocker locker(&m_mutex);
        m_events.clear();
    }
    m_haveLastMousePos = false;
    m_lastMousePos = QPoint();
    m_lastMousePosTime = -1.0;
    m_mouseMovedSinceLastPosition = false;

    QString error;
    QVector<OpenDevice> devices = openInputDevices(&error);
    if (devices.isEmpty()) {
        emit failed(error);
        return;
    }

    emit statusMessage(QString("Recording from %1 input device(s).").arg(devices.size()));

    QVector<pollfd> pfds;
    pfds.reserve(devices.size());
    for (const OpenDevice& dev : devices) {
        pollfd p {};
        p.fd = dev.fd;
        p.events = POLLIN;
        pfds.push_back(p);
    }

    m_timer.start();
    appendMousePosEvent(0.0, true);

    while (!m_stop.loadRelaxed()) {
        const int rc = ::poll(pfds.data(), pfds.size(), 100);
        if (rc < 0) {
            if (errno == EINTR) continue;
            emit failed(QString("poll failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
            break;
        }
        if (rc == 0) continue;

        for (int i = 0; i < pfds.size(); ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;

            while (true) {
                input_event iev {};
                const ssize_t n = ::read(pfds[i].fd, &iev, sizeof(iev));
                if (n == sizeof(iev)) {
                    handleInputEvent(iev);
                    continue;
                }
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                break;
            }
        }
    }

    // Capture the last cursor point if the mouse moved after the last trigger.
    // If this was only movement to the Stop button or tray icon, trimStopButtonTail()
    // removes it together with the final stop click.
    if (m_mouseMovedSinceLastPosition) {
        appendMousePosEvent(m_timer.elapsed() / 1000.0, true);
    }

    trimStopButtonTail(m_trimStopClickSeconds.load(std::memory_order_relaxed));
    closeInputDevices(devices);
    emit statusMessage("Recording stopped.");
}

void EvdevRecorder::appendEvent(const MacroEvent& ev)
{
    int total = 0;
    {
        QMutexLocker locker(&m_mutex);
        m_events.push_back(ev);
        total = m_events.size();
    }
    emit eventRecorded(ev, total);
}

QPoint EvdevRecorder::currentCursorPos(bool* ok) const
{
    if (m_cursorBridge) {
        const QPoint p = m_cursorBridge->cursorPos(ok);
        if (ok && *ok) return p;
    }

    const QPoint result = QCursor::pos();
    if (ok) *ok = true;
    return result;
}

void EvdevRecorder::appendMousePosEvent(double t, bool force)
{
    bool ok = false;
    const QPoint p = currentCursorPos(&ok);
    if (!ok) return;

    if (!force && m_haveLastMousePos && p == m_lastMousePos) return;
    if (m_haveLastMousePos && p == m_lastMousePos && std::abs(t - m_lastMousePosTime) < 0.020) return;

    MacroEvent ev;
    ev.t = std::max(0.0, t);
    ev.type = "mouse_pos";
    ev.args.insert("x", QString::number(p.x()));
    ev.args.insert("y", QString::number(p.y()));
    appendEvent(ev);

    m_haveLastMousePos = true;
    m_lastMousePos = p;
    m_lastMousePosTime = t;
    m_mouseMovedSinceLastPosition = false;
}

void EvdevRecorder::trimStopButtonTail(double secondsBeforeStop)
{
    if (secondsBeforeStop <= 0.0) return;

    QMutexLocker locker(&m_mutex);
    if (m_events.isEmpty()) return;

    const double stopTime = m_timer.isValid() ? (m_timer.elapsed() / 1000.0) : m_events.last().t;
    const double earliest = std::max(0.0, stopTime - secondsBeforeStop);

    int releaseIndex = -1;
    for (int i = m_events.size() - 1; i >= 0; --i) {
        const MacroEvent& ev = m_events[i];
        if (ev.t < earliest) break;
        if (ev.type == "button" && ev.args.value("code").toInt() == BTN_LEFT && ev.args.value("value").toInt() == 0) {
            releaseIndex = i;
            break;
        }
    }
    if (releaseIndex < 0) return;

    int pressIndex = releaseIndex;
    for (int i = releaseIndex - 1; i >= 0; --i) {
        const MacroEvent& ev = m_events[i];
        if (ev.t < earliest) break;
        if (ev.type == "button" && ev.args.value("code").toInt() == BTN_LEFT && ev.args.value("value").toInt() == 1) {
            pressIndex = i;
            break;
        }
    }

    int cutIndex = pressIndex;
    while (cutIndex > 0 &&
           (m_events[cutIndex - 1].type == "mouse_pos" || m_events[cutIndex - 1].type == "rel_move") &&
           (m_events[pressIndex].t - m_events[cutIndex - 1].t) <= secondsBeforeStop) {
        --cutIndex;
    }

    while (m_events.size() > cutIndex) {
        m_events.removeLast();
    }
}

void EvdevRecorder::handleInputEvent(const input_event& iev)
{
    if (iev.type == EV_SYN) return;

    const double t = m_timer.elapsed() / 1000.0;

    if (iev.type == EV_REL) {
        if (iev.code == REL_X || iev.code == REL_Y) {
            m_mouseMovedSinceLastPosition = true;

            // Keep a raw relative fallback in the macro. On KDE Wayland,
            // absolute cursor feedback can be unavailable if the KWin bridge
            // is blocked. These events let playback still reproduce the
            // physical path from the same starting position instead of doing
            // nothing or clicking at random coordinates.
            MacroEvent ev;
            ev.t = t;
            ev.type = "rel_move";
            ev.args.insert("dx", QString::number(iev.code == REL_X ? iev.value : 0));
            ev.args.insert("dy", QString::number(iev.code == REL_Y ? iev.value : 0));
            appendEvent(ev);
            return;
        }

        if (iev.code == REL_WHEEL || iev.code == REL_HWHEEL || iev.code == REL_WHEEL_HI_RES || iev.code == REL_HWHEEL_HI_RES) {
            appendMousePosEvent(t, true);
            MacroEvent ev;
            ev.t = t;
            ev.type = "wheel";
            if (iev.code == REL_WHEEL || iev.code == REL_WHEEL_HI_RES) {
                ev.args.insert("dy", QString::number(iev.value));
                ev.args.insert("hires", iev.code == REL_WHEEL_HI_RES ? "true" : "false");
            } else {
                ev.args.insert("dx", QString::number(iev.value));
                ev.args.insert("hires", iev.code == REL_HWHEEL_HI_RES ? "true" : "false");
            }
            appendEvent(ev);
            return;
        }
    }

    if (iev.type == EV_KEY) {
        MacroEvent ev;
        ev.t = t;
        ev.type = (iev.code >= BTN_MOUSE && iev.code < KEY_OK) ? "button" : "key";
        if (ev.type == "button") {
            appendMousePosEvent(t, true);
        }
        ev.args.insert("code", QString::number(iev.code));
        ev.args.insert("name", inputCodeName(iev.code));
        ev.args.insert("value", QString::number(iev.value)); // 1 down, 0 up, 2 repeat
        appendEvent(ev);
        return;
    }
}
