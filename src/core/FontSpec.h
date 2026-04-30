#pragma once

#include <QString>

namespace bld::core {

// Opaque representation of a System.Drawing.Font as serialized by vanilla BlueBrick.
// We preserve the raw family/size/style trio so round-trip writes emit the same XML
// even if Qt's QFont can't represent the style flags verbatim (e.g. Strikeout+Italic).
struct FontSpec {
    QString familyName = QStringLiteral("Microsoft Sans Serif");
    float   sizePt = 8.25f;
    // C# FontStyle flag-enum rendered as `ToString()`: "Regular", "Bold", "Bold, Italic", etc.
    QString styleString = QStringLiteral("Regular");
};

}
