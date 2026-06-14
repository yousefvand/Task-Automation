#include "MacroFile.h"

#include <QFile>
#include <QTextStream>

QString MacroFile::toText(const MacroEvents& events)
{
    QString out;
    QTextStream s(&out);
    s << "# Task Automation v1\n";
    s << "start\n";
    for (const MacroEvent& ev : events) {
        s << ev.toLine() << "\n";
    }
    s << "end\n";
    return out;
}

bool MacroFile::fromText(const QString& text, MacroEvents& events, QString* error)
{
    events.clear();
    bool seenStart = false;
    bool seenEnd = false;

    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (line == "start") {
            seenStart = true;
            continue;
        }
        if (line == "end") {
            seenEnd = true;
            break;
        }
        if (!seenStart) {
            if (error) *error = QString("line %1 appears before start").arg(i + 1);
            return false;
        }

        MacroEvent ev;
        QString parseError;
        if (!MacroEvent::fromLine(line, ev, &parseError)) {
            if (error) *error = QString("line %1: %2").arg(i + 1).arg(parseError);
            return false;
        }
        events.push_back(ev);
    }

    if (!seenStart) {
        if (error) *error = "missing start";
        return false;
    }
    if (!seenEnd) {
        if (error) *error = "missing end";
        return false;
    }
    return true;
}

bool MacroFile::save(const QString& path, const MacroEvents& events, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    QTextStream s(&f);
    s << toText(events);
    return true;
}

bool MacroFile::load(const QString& path, MacroEvents& events, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QString text = QString::fromUtf8(f.readAll());
    return fromText(text, events, error);
}
