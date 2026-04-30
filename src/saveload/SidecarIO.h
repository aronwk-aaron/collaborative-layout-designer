#pragma once

#include <QByteArray>
#include <QString>

namespace bld::core { struct Sidecar; }

namespace bld::saveload {

struct SidecarLoadResult {
    bool ok = false;
    QString error;
    bool hashMismatch = false;   // true if the stored hash differs from the current .bbm
};

// Compute the canonical sidecar path for a .bbm: "foo.bbm" -> "foo.bbm.bld".
QString sidecarPathFor(const QString& bbmPath);

// Read the sidecar at `cldPath` and populate `out`. If `bbmBytes` is non-empty,
// verify that its SHA-256 matches the hash stored in the sidecar; report a
// mismatch in the result without failing the load (caller decides how to react).
SidecarLoadResult readSidecar(const QString& cldPath,
                               const QByteArray& bbmBytes,
                               core::Sidecar& out);

// Write the sidecar JSON. `bbmBytes` is the just-written .bbm content used to
// stamp the hash so we can detect drift on next load.
bool writeSidecar(const QString& cldPath,
                  const QByteArray& bbmBytes,
                  const core::Sidecar& sidecar,
                  QString* error = nullptr);

// Compute SHA-256 of the bytes (hex-encoded lowercase).
QByteArray sha256Hex(const QByteArray& bytes);

}
