#pragma once

#include <QObject>
#include <QMutex>
#include <QPoint>
#include <QString>
#include <QDBusMessage>

class KWinCursorBridge : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.taskautomation.CursorBridge")

public:
    explicit KWinCursorBridge(QObject* parent = nullptr);
    ~KWinCursorBridge() override;

    bool start(QString* error = nullptr);
    QPoint cursorPos(bool* ok = nullptr) const;
    bool hasLivePosition() const;
    QString statusText() const;

public slots:
    void UpdateCursorPos(int x, int y);
    void UpdateCursorPosText(const QString& x, const QString& y);

signals:
    void statusMessage(const QString& message);

private:
    bool registerDBus(QString* error);
    bool installKWinScript(QString* error);
    bool loadAndRunKWinScript(QString* error);
    bool parseLoadReply(const QDBusMessage& reply, QString* objectPath, int* scriptId, QString* error) const;
    bool startScriptObject(const QString& objectPath, int scriptId, QString* error);
    bool startKWinScripting(QString* error);
    bool runCommand(const QString& program, const QStringList& arguments, QString* output, QString* errorText, int timeoutMs = 5000) const;
    bool waitForInitialCursor(int timeoutMs);
    QString scriptPackagePath() const;
    QString scriptMainPath() const;
    QString scriptMetadataPath() const;

    mutable QMutex m_mutex;
    QPoint m_lastPos;
    bool m_havePos = false;
    bool m_registered = false;
    bool m_scriptLoaded = false;
    bool m_announcedActive = false;
    int m_scriptId = -1;
    QString m_scriptObjectPath;
    QString m_pluginName = "taskautomation-cursor-bridge";
    QString m_dbusServiceName = "org.taskautomation.CursorBridge";
    QString m_dbusTargetName;
    QString m_status;
};
