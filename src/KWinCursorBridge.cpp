#include "KWinCursorBridge.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QVariant>

KWinCursorBridge::KWinCursorBridge(QObject* parent)
    : QObject(parent)
{
    m_pluginName = QString("taskautomation-cursor-bridge-%1").arg(QCoreApplication::applicationPid());
}

KWinCursorBridge::~KWinCursorBridge()
{
    if (!m_scriptObjectPath.isEmpty()) {
        QDBusInterface script("org.kde.KWin",
                              m_scriptObjectPath,
                              "org.kde.kwin.Script",
                              QDBusConnection::sessionBus());
        script.call("stop");
    } else if (m_scriptId >= 0) {
        QDBusInterface script("org.kde.KWin",
                              QString("/Scripting/Script%1").arg(m_scriptId),
                              "org.kde.kwin.Script",
                              QDBusConnection::sessionBus());
        script.call("stop");
    }

    QDBusInterface scripting("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (scripting.isValid()) {
        scripting.call("unloadScript", m_pluginName);
    }

    if (m_registered) {
        QDBusConnection::sessionBus().unregisterObject("/CursorBridge");
        QDBusConnection::sessionBus().unregisterObject("/");
        QDBusConnection::sessionBus().unregisterService(m_dbusServiceName);
    }
}

bool KWinCursorBridge::start(QString* error)
{
    QString dbusError;
    const bool dbusOk = registerDBus(&dbusError);
    if (!dbusOk) {
        m_status = QString("KWin cursor bridge disabled: %1").arg(dbusError);
        if (error) *error = m_status;
        emit statusMessage(m_status);
        return false;
    }

    QString scriptError;
    const bool scriptOk = installKWinScript(&scriptError);
    if (!scriptOk) {
        m_status = QString("KWin cursor bridge unavailable; using raw relative mouse fallback. Details: %1").arg(scriptError);
        if (error) *error = m_status;
        emit statusMessage(m_status);
        return false;
    }

    if (hasLivePosition()) {
        m_status = "KWin cursor bridge active.";
    } else {
        m_status = "KWin cursor bridge loaded; move the mouse once to activate accurate coordinates.";
    }

    if (error) error->clear();
    emit statusMessage(m_status);
    return true;
}

bool KWinCursorBridge::registerDBus(QString* error)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (error) *error = "session D-Bus is not connected";
        return false;
    }

    // Prefer the unique D-Bus name for KWin callbacks. This matches the
    // proven kdotool pattern and avoids stale well-known service ownership.
    m_dbusTargetName = bus.baseService();
    if (m_dbusTargetName.isEmpty()) m_dbusTargetName = m_dbusServiceName;

    bus.unregisterObject("/CursorBridge");
    bus.unregisterObject("/");

    // The KWin script is installed as a normal KWin package. It cannot rely on
    // this process' unique D-Bus name because that changes every restart, so
    // the well-known name is the primary contract.
    if (!bus.registerService(m_dbusServiceName)) {
        if (error) {
            *error = QString("could not own D-Bus service %1; another Task Automation instance may still be running").arg(m_dbusServiceName);
        }
        return false;
    }

    const auto flags = QDBusConnection::ExportAllSlots |
                       QDBusConnection::ExportScriptableSlots;

    if (!bus.registerObject("/CursorBridge", this, flags)) {
        bus.unregisterService(m_dbusServiceName);
        if (error) *error = "could not register /CursorBridge D-Bus object";
        return false;
    }

    // Keep root path too because several KWin-script examples use it.
    bus.registerObject("/", this, flags);

    m_registered = true;
    return true;
}

QString KWinCursorBridge::scriptPackagePath() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + "/.local/share";
    }
    return QDir(base).filePath("kwin/scripts/taskautomation-cursor-bridge");
}

QString KWinCursorBridge::scriptMainPath() const
{
    return QDir(scriptPackagePath()).filePath("contents/code/main.js");
}

QString KWinCursorBridge::scriptMetadataPath() const
{
    return QDir(scriptPackagePath()).filePath("metadata.json");
}

bool KWinCursorBridge::installKWinScript(QString* error)
{
    const QString packagePath = scriptPackagePath();
    const QString codeDir = QDir(packagePath).filePath("contents/code");
    if (!QDir().mkpath(codeDir)) {
        if (error) *error = QString("could not create KWin script directory: %1").arg(codeDir);
        return false;
    }

    QFile metadata(scriptMetadataPath());
    if (!metadata.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) *error = QString("could not write KWin bridge metadata: %1").arg(metadata.errorString());
        return false;
    }
    QTextStream ms(&metadata);
    ms << R"JSON({
    "KPlugin": {
        "Name": "Task Automation Cursor Bridge",
        "Description": "Reports the KDE Wayland cursor position to Task Automation over D-Bus.",
        "Icon": "input-mouse",
        "Authors": [
            {
                "Name": "Task Automation"
            }
        ],
        "Id": "taskautomation-cursor-bridge",
        "Version": "1.0",
        "License": "MIT"
    },
    "X-Plasma-API": "javascript",
    "X-Plasma-MainScript": "code/main.js",
    "KPackageStructure": "KWin/Script"
}
)JSON";
    metadata.close();

    QFile f(scriptMainPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) *error = QString("could not write KWin bridge script: %1").arg(f.errorString());
        return false;
    }

    QTextStream s(&f);
    s << "// Task Automation cursor bridge for KDE/KWin Wayland.\n";
    s << "// Loaded as a temporary KWin script. It sends compositor cursor coordinates to the app.\n\n";
    s << R"JS(
var taskAutomationDbusService = ")JS" << m_dbusTargetName << R"JS(";
var taskAutomationWellKnownService = "org.taskautomation.CursorBridge";
var taskAutomationDbusPath = "/CursorBridge";
var taskAutomationDbusInterface = "org.taskautomation.CursorBridge";
var taskAutomationLastX = null;
var taskAutomationLastY = null;

function taskAutomationSend(x, y) {
    var sx = String(Math.round(x));
    var sy = String(Math.round(y));

    print("TaskAutomationCursorBridge: cursor " + sx + "," + sy);

    // First use the unique session-bus name and root path, matching the
    // kdotool Plasma 6 pattern. Then try the explicit exported object and
    // finally the well-known service as a fallback.
    try {
        callDBus(taskAutomationDbusService, "/", "", "UpdateCursorPosText", sx, sy);
        return;
    } catch (e0) {
        print("TaskAutomationCursorBridge: unique/root call failed: " + e0);
    }

    try {
        callDBus(taskAutomationDbusService,
                 taskAutomationDbusPath,
                 taskAutomationDbusInterface,
                 "UpdateCursorPosText",
                 sx,
                 sy);
        return;
    } catch (e1) {
        print("TaskAutomationCursorBridge: unique/explicit D-Bus call failed: " + e1);
    }

    try {
        callDBus(taskAutomationWellKnownService,
                 taskAutomationDbusPath,
                 taskAutomationDbusInterface,
                 "UpdateCursorPosText",
                 sx,
                 sy);
    } catch (e2) {
        print("TaskAutomationCursorBridge: well-known D-Bus call failed: " + e2);
    }
}

function taskAutomationPublishCursorPos() {
    try {
        var p = workspace.cursorPos;
        if (p === undefined || p === null) {
            print("TaskAutomationCursorBridge: workspace.cursorPos is unavailable");
            return;
        }

        var x = Number(p.x);
        var y = Number(p.y);
        if (!isFinite(x) || !isFinite(y)) {
            print("TaskAutomationCursorBridge: invalid cursorPos " + p);
            return;
        }

        if (taskAutomationLastX === x && taskAutomationLastY === y) {
            return;
        }

        taskAutomationLastX = x;
        taskAutomationLastY = y;
        taskAutomationSend(x, y);
    } catch (e) {
        print("TaskAutomationCursorBridge error: " + e);
    }
}

try {
    workspace.cursorPosChanged.connect(taskAutomationPublishCursorPos);
    print("TaskAutomationCursorBridge: connected cursorPosChanged");
} catch (e) {
    print("TaskAutomationCursorBridge: cursorPosChanged connect failed: " + e);
}

print("TaskAutomationCursorBridge: loaded");
taskAutomationPublishCursorPos();
)JS";
    f.close();

    return loadAndRunKWinScript(error);
}

bool KWinCursorBridge::runCommand(const QString& program,
                                  const QStringList& arguments,
                                  QString* output,
                                  QString* errorText,
                                  int timeoutMs) const
{
    const QString exe = QStandardPaths::findExecutable(program);
    if (exe.isEmpty()) {
        if (errorText) *errorText = QString("%1 not found").arg(program);
        return false;
    }

    QProcess process;
    process.start(exe, arguments);
    if (!process.waitForStarted(timeoutMs)) {
        if (errorText) *errorText = QString("%1 did not start: %2").arg(program, process.errorString());
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        if (errorText) *errorText = QString("%1 timed out").arg(program);
        return false;
    }

    const QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError());
    if (output) *output = stdOut + stdErr;

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorText) {
            *errorText = QString("%1 failed with exit %2: %3").arg(program).arg(process.exitCode()).arg((stdOut + stdErr).trimmed());
        }
        return false;
    }

    return true;
}

bool KWinCursorBridge::parseLoadReply(const QDBusMessage& reply, QString* objectPath, int* scriptId, QString* error) const
{
    if (objectPath) objectPath->clear();
    if (scriptId) *scriptId = -1;

    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error) *error = reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage();
        return false;
    }

    const QList<QVariant> args = reply.arguments();
    if (args.isEmpty()) {
        // Some KWin builds accept loadScript without returning an id. In that
        // case /Scripting.start may still activate it.
        return true;
    }

    bool sawFailureValue = false;
    QStringList debugValues;

    for (const QVariant& v : args) {
        debugValues << QString("%1=%2").arg(QString::fromLatin1(v.typeName()), v.toString());

        if (v.typeId() == QMetaType::Bool) {
            if (!v.toBool()) sawFailureValue = true;
            continue;
        }

        if (v.canConvert<QDBusObjectPath>()) {
            const QString path = qvariant_cast<QDBusObjectPath>(v).path();
            if (!path.isEmpty()) {
                if (objectPath) *objectPath = path;
                return true;
            }
        }

        bool okInt = false;
        const int id = v.toInt(&okInt);
        if (okInt) {
            if (id >= 0) {
                if (scriptId) *scriptId = id;
                if (objectPath) *objectPath = QString("/Scripting/Script%1").arg(id);
                return true;
            }
            sawFailureValue = true;
            continue;
        }

        const QString text = v.toString();
        if (!text.isEmpty()) {
            if (text.startsWith('/')) {
                if (objectPath) *objectPath = text;
                return true;
            }
            bool okTextInt = false;
            const int textId = text.toInt(&okTextInt);
            if (okTextInt) {
                if (textId >= 0) {
                    if (scriptId) *scriptId = textId;
                    if (objectPath) *objectPath = QString("/Scripting/Script%1").arg(textId);
                    return true;
                }
                sawFailureValue = true;
            }
        }
    }

    if (sawFailureValue) {
        if (error) *error = QString("loadScript returned failure value(s): %1").arg(debugValues.join(", "));
        return false;
    }

    return true;
}

bool KWinCursorBridge::startScriptObject(const QString& objectPath, int scriptId, QString* error)
{
    QString path = objectPath;
    if (path.isEmpty() && scriptId >= 0) {
        path = QString("/Scripting/Script%1").arg(scriptId);
    }
    if (path.isEmpty()) return false;

    QDBusInterface script("org.kde.KWin",
                          path,
                          "org.kde.kwin.Script",
                          QDBusConnection::sessionBus());
    if (!script.isValid()) {
        if (error) *error = QString("KWin script object %1 was not found").arg(path);
        return false;
    }

    QDBusReply<void> runReply = script.call("run");
    if (!runReply.isValid()) {
        const QString msg = runReply.error().message().isEmpty() ? runReply.error().name() : runReply.error().message();
        if (error) *error = QString("KWin script run failed: %1").arg(msg);
        return false;
    }

    m_scriptObjectPath = path;
    if (scriptId >= 0) m_scriptId = scriptId;
    return true;
}

bool KWinCursorBridge::startKWinScripting(QString* error)
{
    QDBusInterface scripting("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    if (!scripting.isValid()) {
        if (error) *error = "KWin scripting D-Bus interface was not found";
        return false;
    }

    QDBusReply<void> startReply = scripting.call("start");
    if (!startReply.isValid()) {
        const QString msg = startReply.error().message().isEmpty() ? startReply.error().name() : startReply.error().message();
        if (error) *error = QString("KWin /Scripting.start failed: %1").arg(msg);
        return false;
    }
    return true;
}

bool KWinCursorBridge::waitForInitialCursor(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        {
            QMutexLocker locker(&m_mutex);
            if (m_havePos) return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(20);
    }
    QMutexLocker locker(&m_mutex);
    return m_havePos;
}

bool KWinCursorBridge::loadAndRunKWinScript(QString* error)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface scripting("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", bus);
    if (!scripting.isValid()) {
        if (error) *error = "KWin scripting D-Bus interface was not found";
        return false;
    }

    const QString scriptPath = scriptMainPath();

    // Follow the proven Plasma 6/kdotool flow: load a JavaScript file through
    // /Scripting.loadScript, accept script id 0 as valid, then run
    // /Scripting/ScriptN. Keeping the file in ~/.local/share is fine; the
    // important part is that KWin gets a JS file path, not a package directory.
    scripting.call("unloadScript", m_pluginName);

    QDBusMessage loadReply = scripting.call("loadScript", scriptPath, m_pluginName);
    QString objectPath;
    int scriptId = -1;
    QString parseError;
    const bool loadOk = parseLoadReply(loadReply, &objectPath, &scriptId, &parseError);
    if (!loadOk) {
        if (error) {
            *error = QString("KWin loadScript failed: %1 for %2").arg(parseError, scriptPath);
        }
        return false;
    }

    QString runError;
    if (!startScriptObject(objectPath, scriptId, &runError)) {
        if (error) {
            *error = QString("KWin script loaded but could not run: %1. scriptPath=%2 scriptId=%3 objectPath=%4")
                         .arg(runError, scriptPath)
                         .arg(scriptId)
                         .arg(objectPath);
        }
        return false;
    }

    if (waitForInitialCursor(2500)) {
        m_scriptLoaded = true;
        return true;
    }

    if (error) {
        *error = QString("KWin script ran but did not send cursor position. Move the mouse once. If it stays inactive, run: journalctl --user -u plasma-kwin_wayland.service -f | grep TaskAutomationCursorBridge");
    }
    return false;
}

void KWinCursorBridge::UpdateCursorPos(int x, int y)
{
    bool first = false;
    {
        QMutexLocker locker(&m_mutex);
        first = !m_havePos;
        m_lastPos = QPoint(x, y);
        m_havePos = true;
    }

    if (first || !m_announcedActive) {
        m_announcedActive = true;
        m_status = "KWin cursor bridge active.";
        emit statusMessage(m_status);
    }
}

void KWinCursorBridge::UpdateCursorPosText(const QString& x, const QString& y)
{
    bool okX = false;
    bool okY = false;
    const int ix = x.toInt(&okX);
    const int iy = y.toInt(&okY);
    if (okX && okY) {
        UpdateCursorPos(ix, iy);
    }
}

QPoint KWinCursorBridge::cursorPos(bool* ok) const
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_havePos) {
            if (ok) *ok = true;
            return m_lastPos;
        }
    }

    if (ok) *ok = false;
    return QCursor::pos();
}

bool KWinCursorBridge::hasLivePosition() const
{
    QMutexLocker locker(&m_mutex);
    return m_havePos;
}

QString KWinCursorBridge::statusText() const
{
    return m_status;
}
