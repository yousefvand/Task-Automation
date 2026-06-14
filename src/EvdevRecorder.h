#pragma once

#include "MacroEvent.h"

#include <QAtomicInteger>
#include <QElapsedTimer>
#include <QMutex>
#include <QPoint>
#include <QThread>
#include <QStringList>
#include <atomic>
#include <linux/input.h>

class KWinCursorBridge;

class EvdevRecorder : public QThread
{
    Q_OBJECT
public:
    explicit EvdevRecorder(QObject* parent = nullptr);
    ~EvdevRecorder() override;

    void requestStop(double trimStopClickSeconds = 0.0);
    MacroEvents events() const;

    // Kept for old callers. Task Automation now records coordinate trigger points
    // instead of raw relative mouse spam, so this is intentionally a no-op.
    void setCompressMouseMoves(bool enabled);
    void setCursorBridge(KWinCursorBridge* bridge);

signals:
    void statusMessage(const QString& message);
    void eventRecorded(const MacroEvent& event, int totalCount);
    void failed(const QString& error);

protected:
    void run() override;

private:
    struct OpenDevice {
        int fd = -1;
        QString path;
        QString name;
    };

    QVector<OpenDevice> openInputDevices(QString* error);
    void closeInputDevices(QVector<OpenDevice>& devices);
    void handleInputEvent(const input_event& iev);
    void appendEvent(const MacroEvent& ev);
    void appendMousePosEvent(double t, bool force = false);
    QPoint currentCursorPos(bool* ok = nullptr) const;
    void trimStopButtonTail(double secondsBeforeStop);

    QAtomicInteger<bool> m_stop { false };
    std::atomic<double> m_trimStopClickSeconds { 0.0 };
    mutable QMutex m_mutex;
    MacroEvents m_events;
    QElapsedTimer m_timer;

    bool m_haveLastMousePos = false;
    QPoint m_lastMousePos;
    double m_lastMousePosTime = -1.0;
    bool m_mouseMovedSinceLastPosition = false;
    KWinCursorBridge* m_cursorBridge = nullptr;
};
