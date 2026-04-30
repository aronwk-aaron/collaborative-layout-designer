#pragma once

#include <QColor>
#include <QString>

namespace bld::core {

// Vanilla BlueBrick's <Color><IsKnownColor>bool</IsKnownColor><Name/></Color>
// can carry either a named System.Drawing.KnownColor (e.g. "CornflowerBlue",
// "Black") or an ARGB hex string. We need to round-trip both forms so files
// re-saved by our writer match vanilla byte-for-byte — losing the known-name
// distinction would bloat save output by ~150 bytes per file and make
// byte-diff CI fail immediately.
struct ColorSpec {
    QColor  color = Qt::black;
    QString knownName;  // empty when not a known color; set preserves the original name verbatim

    bool isKnown() const { return !knownName.isEmpty(); }

    static ColorSpec fromKnown(QColor c, QString name) { return { c, std::move(name) }; }
    static ColorSpec fromArgb(QColor c) { return { c, {} }; }

    bool operator==(const ColorSpec& other) const {
        return color.rgba() == other.color.rgba() && knownName == other.knownName;
    }
};

}
