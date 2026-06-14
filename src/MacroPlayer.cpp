#include "MacroPlayer.h"
#include "UInputDevice.h"
#include "KWinCursorBridge.h"

#include <QCoreApplication>
#include <QCursor>
#include <QMetaObject>
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <cmath>
#include <QThread>
#include <linux/input.h>

MacroPlayer::MacroPlayer(QObject* parent) : QThread(parent) {}

MacroPlayer::~MacroPlayer()
{
    requestStop();
    wait(2000);
}

void MacroPlayer::setMacro(const MacroEvents& events)
{
    m_events = events;
    std::sort(m_events.begin(), m_events.end(), [](const MacroEvent& a, const MacroEvent& b) {
        return a.t < b.t;
    });
}

void MacroPlayer::setOptions(const Options& options)
{
    m_options = options;
    if (m_options.speed <= 0.0) m_options.speed = 1.0;
    if (m_options.replayCount < 1) m_options.replayCount = 1;
    if (m_options.replayGapSeconds < 0.0) m_options.replayGapSeconds = 0.0;
}

void MacroPlayer::requestStop()
{
    m_stop.storeRelaxed(true);
}

void MacroPlayer::setCursorBridge(KWinCursorBridge* bridge)
{
    m_cursorBridge = bridge;
}

bool MacroPlayer::sleepInterruptible(double seconds)
{
    if (seconds <= 0.0) return !m_stop.loadRelaxed();
    int remainingMs = static_cast<int>(seconds * 1000.0);
    while (remainingMs > 0 && !m_stop.loadRelaxed()) {
        const int step = std::min(remainingMs, 25);
        QThread::msleep(static_cast<unsigned long>(step));
        remainingMs -= step;
    }
    return !m_stop.loadRelaxed();
}

QRect MacroPlayer::virtualScreenGeometry() const
{
    if (m_options.screenGeometry.isValid()) return m_options.screenGeometry;
    QRect g;
    const auto screens = QGuiApplication::screens();
    for (QScreen* screen : screens) {
        if (!screen) continue;
        g = g.isNull() ? screen->geometry() : g.united(screen->geometry());
    }
    if (g.isNull()) g = QRect(0, 0, 1920, 1080);
    return g;
}

QPoint MacroPlayer::currentCursorPos(bool* ok) const
{
    if (m_cursorBridge) {
        const QPoint p = m_cursorBridge->cursorPos(ok);
        if (ok && *ok) return p;
    }

    const QPoint result = QCursor::pos();
    if (ok) *ok = true;
    return result;
}

bool MacroPlayer::moveToPosition(const QPoint& target, UInputDevice& device, QString* error)
{
    Q_UNUSED(error);

    const bool haveLiveBridge = m_cursorBridge && m_cursorBridge->hasLivePosition();
    const QString platform = QGuiApplication::platformName().toLower();

    // On Wayland, QCursor::pos() is often stale or blocked for global desktop
    // coordinates. If the KWin bridge is not active, do not pretend we can
    // verify absolute position. Treat mouse_pos as a marker and rely on the
    // recorded rel_move events around it. This prevents false "cursor did not
    // move" failures and avoids random corrective movement.
    if (!haveLiveBridge && platform.contains("wayland")) {
        // Last-resort Wayland fallback: clamp the virtual pointer to the top-left
        // edge, then walk it to the requested recorded coordinate with tiny
        // relative steps. This is slower than the KWin bridge, but much more
        // accurate than replaying accumulated raw relative deltas from an
        // unknown starting position.
        return moveToPositionByCornerClamp(target, device, error);
    }

    // Feedback loop using relative motion. This is used when the KWin bridge
    // gives live compositor cursor position, or on X11 where QCursor::pos()
    // can report global cursor movement.
    int unchangedCount = 0;
    QPoint lastPos;
    bool haveLast = false;

    for (int i = 0; i < 140 && !m_stop.loadRelaxed(); ++i) {
        bool ok = false;
        const QPoint current = currentCursorPos(&ok);
        if (!ok) {
            if (error) *error = "Could not read current cursor position. Run as your normal Wayland user after installing the udev rule.";
            return false;
        }

        const int dx = target.x() - current.x();
        const int dy = target.y() - current.y();
        if (std::abs(dx) <= 2 && std::abs(dy) <= 2) return true;

        if (haveLast && current == lastPos) {
            ++unchangedCount;
        } else {
            unchangedCount = 0;
        }
        haveLast = true;
        lastPos = current;

        if (unchangedCount > 18) {
            if (error) *error = "Cursor feedback did not change during playback. If you are on Wayland, install/run with input permissions and keep the KWin cursor bridge active.";
            return false;
        }

        auto stepFor = [](int delta) {
            const int magnitude = std::abs(delta);
            if (magnitude <= 2) return delta;
            if (magnitude < 10) return (delta > 0) ? 1 : -1;
            if (magnitude < 80) return std::clamp(delta / 2, -20, 20);
            return std::clamp(delta / 3, -40, 40);
        };

        if (!device.emitRelativeMove(stepFor(dx), stepFor(dy), error)) return false;
        QThread::msleep(4);
    }

    if (error) *error = "Could not reach target cursor position before timeout.";
    return false;
}

bool MacroPlayer::moveToPositionByCornerClamp(const QPoint& target, UInputDevice& device, QString* error)
{
    const QRect screen = virtualScreenGeometry();
    const int x = std::clamp(target.x() - screen.left(), 0, std::max(0, screen.width() - 1));
    const int y = std::clamp(target.y() - screen.top(), 0, std::max(0, screen.height() - 1));

    // Slam far beyond the top-left corner. The compositor clamps at the edge.
    // Repeat a few times to overcome pointer acceleration and multi-screen edge
    // behaviour.
    for (int i = 0; i < 4 && !m_stop.loadRelaxed(); ++i) {
        if (!device.emitRelativeMove(-20000, -20000, error)) return false;
        QThread::msleep(8);
    }

    int remainingX = x;
    int remainingY = y;
    int emitted = 0;
    while ((remainingX > 0 || remainingY > 0) && !m_stop.loadRelaxed()) {
        const int dx = remainingX > 0 ? 1 : 0;
        const int dy = remainingY > 0 ? 1 : 0;
        if (!device.emitRelativeMove(dx, dy, error)) return false;
        remainingX -= dx;
        remainingY -= dy;
        ++emitted;

        // Tiny pacing keeps libinput from treating the sequence as a fast mouse
        // fling. It is still fast enough for normal automation coordinates.
        if ((emitted % 80) == 0) {
            QThread::usleep(1000);
        }
    }

    return !m_stop.loadRelaxed();
}

bool MacroPlayer::playEvent(const MacroEvent& ev, UInputDevice& device, QString* error)
{
    if (ev.type == "mouse_pos") {
        bool okX = false;
        bool okY = false;
        const int x = ev.args.value("x", "0").toInt(&okX);
        const int y = ev.args.value("y", "0").toInt(&okY);
        if (!okX || !okY) {
            if (error) *error = "mouse_pos event missing x/y";
            return false;
        }
        return moveToPosition(QPoint(x, y), device, error);
    }

    if (ev.type == "rel_move") {
        // When the KWin bridge is live, click/wheel trigger points are replayed
        // with compositor cursor feedback. Ignore free mouse movement between
        // triggers, otherwise raw relative deltas fight the coordinate corrector
        // and libinput acceleration makes the cursor drift. Keep relative moves
        // while a mouse button is held so drag operations still work.
        const bool haveLiveBridge = m_cursorBridge && m_cursorBridge->hasLivePosition();
        const bool wayland = QGuiApplication::platformName().toLower().contains("wayland");
        if ((haveLiveBridge || wayland) && m_mouseButtonsDown <= 0) {
            return true;
        }
        const int dx = ev.args.value("dx", "0").toInt();
        const int dy = ev.args.value("dy", "0").toInt();
        return playRelativeMove(dx, dy, device, error);
    }

    if (ev.type == "wheel") {
        const int dx = ev.args.value("dx", "0").toInt();
        const int dy = ev.args.value("dy", "0").toInt();
        const bool hires = ev.args.value("hires", "false") == "true";
        return device.emitWheel(dx, dy, hires, error);
    }

    if (ev.type == "button" || ev.type == "key") {
        const int code = ev.args.value("code", "-1").toInt();
        const int value = ev.args.value("value", "0").toInt();
        if (code < 0) {
            if (error) *error = "button/key event missing code";
            return false;
        }
        const bool ok = device.emitEvent(EV_KEY, code, value, error);
        if (ok && ev.type == "button") {
            if (value == 1) {
                ++m_mouseButtonsDown;
            } else if (value == 0 && m_mouseButtonsDown > 0) {
                --m_mouseButtonsDown;
            }
        }
        return ok;
    }

    // Unknown event types are ignored to keep the format forward-compatible.
    return true;
}

bool MacroPlayer::playRelativeMove(int dx, int dy, UInputDevice& device, QString* error)
{
    int remainingX = dx;
    int remainingY = dy;
    while ((remainingX != 0 || remainingY != 0) && !m_stop.loadRelaxed()) {
        const int stepX = std::clamp(remainingX, -6, 6);
        const int stepY = std::clamp(remainingY, -6, 6);
        if (!device.emitRelativeMove(stepX, stepY, error)) return false;
        remainingX -= stepX;
        remainingY -= stepY;
        if (remainingX != 0 || remainingY != 0) {
            QThread::usleep(1500);
        }
    }
    return !m_stop.loadRelaxed();
}

bool MacroPlayer::hasAnotherReplay(int completed) const
{
    if (m_options.infinite) return true;
    return completed < m_options.replayCount;
}

void MacroPlayer::run()
{
    m_stop.storeRelaxed(false);
    m_mouseButtonsDown = 0;
    emit playbackStarted();

    if (m_events.isEmpty()) {
        emit failed("No macro events to play.");
        emit playbackFinished();
        return;
    }

    const QRect screen = virtualScreenGeometry();
    UInputDevice device;
    QString openError;
    if (!device.openDevice(&openError, std::max(1, screen.width() - 1), std::max(1, screen.height() - 1))) {
        emit failed(openError);
        emit playbackFinished();
        return;
    }

    int replay = 0;
    while (!m_stop.loadRelaxed()) {
        ++replay;
        emit replayStarted(replay);
        emit statusMessage(QString("Playing replay %1%2")
                           .arg(replay)
                           .arg(m_options.infinite ? QString(" (infinite)") : QString("/%1").arg(m_options.replayCount)));

        double previousT = 0.0;
        for (const MacroEvent& ev : m_events) {
            const double delay = std::max(0.0, (ev.t - previousT) / m_options.speed);
            previousT = ev.t;
            if (!sleepInterruptible(delay)) break;

            QString error;
            if (!playEvent(ev, device, &error)) {
                emit failed(error);
                emit playbackFinished();
                return;
            }
        }

        if (m_stop.loadRelaxed()) break;
        if (!hasAnotherReplay(replay)) break;
        if (!sleepInterruptible(m_options.replayGapSeconds)) break;
    }

    emit statusMessage(m_stop.loadRelaxed() ? "Playback stopped." : "Playback finished.");
    emit playbackFinished();
}
