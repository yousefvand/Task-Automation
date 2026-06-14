#include "MacroEvent.h"

#include <QStringList>
#include <QRegularExpression>
#include <linux/input-event-codes.h>

static QString escapeValue(QString v)
{
    v.replace("\\", "\\\\");
    v.replace(" ", "\\s");
    return v;
}

static QString unescapeValue(QString v)
{
    v.replace("\\s", " ");
    v.replace("\\\\", "\\");
    return v;
}

QString MacroEvent::toLine() const
{
    QStringList parts;
    parts << QString("t=%1").arg(t, 0, 'f', 6);
    parts << type;

    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        parts << QString("%1=%2").arg(it.key(), escapeValue(it.value()));
    }
    return parts.join(' ');
}

bool MacroEvent::fromLine(const QString& line, MacroEvent& out, QString* error)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed == "start" || trimmed == "end") {
        if (error) *error = "not an event line";
        return false;
    }

    const QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() < 2 || !parts[0].startsWith("t=")) {
        if (error) *error = "expected: t=<seconds> <event_type> ...";
        return false;
    }

    bool ok = false;
    const double t = parts[0].mid(2).toDouble(&ok);
    if (!ok || t < 0.0) {
        if (error) *error = "invalid timestamp";
        return false;
    }

    MacroEvent ev;
    ev.t = t;
    ev.type = parts[1];

    for (int i = 2; i < parts.size(); ++i) {
        const int eq = parts[i].indexOf('=');
        if (eq <= 0) {
            if (error) *error = QString("invalid key=value token: %1").arg(parts[i]);
            return false;
        }
        const QString key = parts[i].left(eq);
        const QString value = unescapeValue(parts[i].mid(eq + 1));
        ev.args.insert(key, value);
    }

    out = ev;
    return true;
}

QString inputCodeName(int code)
{
    switch (code) {
    case KEY_ESC: return "KEY_ESC";
    case KEY_1: return "KEY_1";
    case KEY_2: return "KEY_2";
    case KEY_3: return "KEY_3";
    case KEY_4: return "KEY_4";
    case KEY_5: return "KEY_5";
    case KEY_6: return "KEY_6";
    case KEY_7: return "KEY_7";
    case KEY_8: return "KEY_8";
    case KEY_9: return "KEY_9";
    case KEY_0: return "KEY_0";
    case KEY_MINUS: return "KEY_MINUS";
    case KEY_EQUAL: return "KEY_EQUAL";
    case KEY_BACKSPACE: return "KEY_BACKSPACE";
    case KEY_TAB: return "KEY_TAB";
    case KEY_Q: return "KEY_Q";
    case KEY_W: return "KEY_W";
    case KEY_E: return "KEY_E";
    case KEY_R: return "KEY_R";
    case KEY_T: return "KEY_T";
    case KEY_Y: return "KEY_Y";
    case KEY_U: return "KEY_U";
    case KEY_I: return "KEY_I";
    case KEY_O: return "KEY_O";
    case KEY_P: return "KEY_P";
    case KEY_LEFTBRACE: return "KEY_LEFTBRACE";
    case KEY_RIGHTBRACE: return "KEY_RIGHTBRACE";
    case KEY_ENTER: return "KEY_ENTER";
    case KEY_LEFTCTRL: return "KEY_LEFTCTRL";
    case KEY_A: return "KEY_A";
    case KEY_S: return "KEY_S";
    case KEY_D: return "KEY_D";
    case KEY_F: return "KEY_F";
    case KEY_G: return "KEY_G";
    case KEY_H: return "KEY_H";
    case KEY_J: return "KEY_J";
    case KEY_K: return "KEY_K";
    case KEY_L: return "KEY_L";
    case KEY_SEMICOLON: return "KEY_SEMICOLON";
    case KEY_APOSTROPHE: return "KEY_APOSTROPHE";
    case KEY_GRAVE: return "KEY_GRAVE";
    case KEY_LEFTSHIFT: return "KEY_LEFTSHIFT";
    case KEY_BACKSLASH: return "KEY_BACKSLASH";
    case KEY_Z: return "KEY_Z";
    case KEY_X: return "KEY_X";
    case KEY_C: return "KEY_C";
    case KEY_V: return "KEY_V";
    case KEY_B: return "KEY_B";
    case KEY_N: return "KEY_N";
    case KEY_M: return "KEY_M";
    case KEY_COMMA: return "KEY_COMMA";
    case KEY_DOT: return "KEY_DOT";
    case KEY_SLASH: return "KEY_SLASH";
    case KEY_RIGHTSHIFT: return "KEY_RIGHTSHIFT";
    case KEY_LEFTALT: return "KEY_LEFTALT";
    case KEY_SPACE: return "KEY_SPACE";
    case KEY_CAPSLOCK: return "KEY_CAPSLOCK";
    case KEY_F1: return "KEY_F1";
    case KEY_F2: return "KEY_F2";
    case KEY_F3: return "KEY_F3";
    case KEY_F4: return "KEY_F4";
    case KEY_F5: return "KEY_F5";
    case KEY_F6: return "KEY_F6";
    case KEY_F7: return "KEY_F7";
    case KEY_F8: return "KEY_F8";
    case KEY_F9: return "KEY_F9";
    case KEY_F10: return "KEY_F10";
    case KEY_F11: return "KEY_F11";
    case KEY_F12: return "KEY_F12";
    case KEY_RIGHTCTRL: return "KEY_RIGHTCTRL";
    case KEY_RIGHTALT: return "KEY_RIGHTALT";
    case KEY_LEFTMETA: return "KEY_LEFTMETA";
    case KEY_RIGHTMETA: return "KEY_RIGHTMETA";
    case BTN_LEFT: return "BTN_LEFT";
    case BTN_RIGHT: return "BTN_RIGHT";
    case BTN_MIDDLE: return "BTN_MIDDLE";
    case BTN_SIDE: return "BTN_SIDE";
    case BTN_EXTRA: return "BTN_EXTRA";
    default: return QString("CODE_%1").arg(code);
    }
}

QString eventTypeName(int evType)
{
    switch (evType) {
    case EV_KEY: return "EV_KEY";
    case EV_REL: return "EV_REL";
    case EV_ABS: return "EV_ABS";
    case EV_SYN: return "EV_SYN";
    default: return QString("EV_%1").arg(evType);
    }
}
