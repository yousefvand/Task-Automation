#pragma once

#include "MacroEvent.h"

#include <QAtomicInteger>
#include <QPoint>
#include <QRect>
#include <QThread>

class UInputDevice;
class KWinCursorBridge;

class MacroPlayer : public QThread
{
    Q_OBJECT
public:
    struct Options {
        double speed = 1.0;
        int replayCount = 1;
        bool infinite = false;
        double replayGapSeconds = 0.0;
        QRect screenGeometry;
    };

    explicit MacroPlayer(QObject* parent = nullptr);
    ~MacroPlayer() override;

    void setMacro(const MacroEvents& events);
    void setOptions(const Options& options);
    void requestStop();
    void setCursorBridge(KWinCursorBridge* bridge);

signals:
    void statusMessage(const QString& message);
    void failed(const QString& error);
    void playbackStarted();
    void playbackFinished();
    void replayStarted(int index);

protected:
    void run() override;

private:
    bool sleepInterruptible(double seconds);
    bool playEvent(const MacroEvent& ev, UInputDevice& device, QString* error);
    bool playRelativeMove(int dx, int dy, UInputDevice& device, QString* error);
    bool moveToPosition(const QPoint& target, UInputDevice& device, QString* error);
    bool moveToPositionByCornerClamp(const QPoint& target, UInputDevice& device, QString* error);
    QPoint currentCursorPos(bool* ok = nullptr) const;
    QRect virtualScreenGeometry() const;
    bool hasAnotherReplay(int completed) const;

    QAtomicInteger<bool> m_stop { false };
    MacroEvents m_events;
    Options m_options;
    KWinCursorBridge* m_cursorBridge = nullptr;
    int m_mouseButtonsDown = 0;
};
