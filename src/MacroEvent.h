#pragma once

#include <QString>
#include <QMap>
#include <QVector>
#include <QMetaType>

struct MacroEvent
{
    double t = 0.0;
    QString type;
    QMap<QString, QString> args;

    QString toLine() const;
    static bool fromLine(const QString& line, MacroEvent& out, QString* error = nullptr);
};

using MacroEvents = QVector<MacroEvent>;

QString inputCodeName(int code);
QString eventTypeName(int evType);

Q_DECLARE_METATYPE(MacroEvent)
