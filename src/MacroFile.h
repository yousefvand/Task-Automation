#pragma once

#include "MacroEvent.h"
#include <QString>

namespace MacroFile
{
    QString toText(const MacroEvents& events);
    bool fromText(const QString& text, MacroEvents& events, QString* error = nullptr);
    bool save(const QString& path, const MacroEvents& events, QString* error = nullptr);
    bool load(const QString& path, MacroEvents& events, QString* error = nullptr);
}
