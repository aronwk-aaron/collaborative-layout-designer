#!/usr/bin/env bash
# Launch vanilla BlueBrick 1.9.2.0 under wine / Proton-GE against a .bbm
# from this fork. Used for the forward-compat manual check — verify a
# file written by BLD still opens cleanly in vanilla.
#
# Prereqs:
#   - BlueBrick 1.9.2.0 installed somewhere on disk (point BLUEBRICK_EXE
#     at it, or drop the app under ~/Documents/BlueBrick.1.9.2/ and we
#     auto-detect).
#   - Either:
#       a) wine (/usr/bin/wine — works for BlueBrick out of the box on
#          most distros; needs .NET Framework 4.8 in the prefix which
#          `winetricks dotnet48` installs if missing), OR
#       b) Proton-GE installed via Steam's compatibilitytools.d. Set
#          PROTON=1 to prefer Proton; defaults to wine.
#
# Usage:
#   scripts/run-vanilla-bluebrick.sh path/to/file.bbm
#   PROTON=1 scripts/run-vanilla-bluebrick.sh path/to/file.bbm
#   BLUEBRICK_EXE=/custom/BlueBrick.exe scripts/run-vanilla-bluebrick.sh file.bbm
#
# Exit code:
#   0 — BlueBrick.exe was launched. Inspect the resulting window for
#       any error dialog; close vanilla when done. The script itself
#       can't detect a parse error (no CLI output from Windows Forms);
#       that's still a visual check.
#   1 — BlueBrick executable not found or args wrong.
#
# See docs/MANUAL_TESTING.md §4.5 for the forward-compat checklist
# this script backs.

set -euo pipefail

if [[ $# -lt 1 ]]; then
    cat >&2 <<'USAGE'
Usage: run-vanilla-bluebrick.sh <file.bbm>
       PROTON=1 run-vanilla-bluebrick.sh <file.bbm>
       BLUEBRICK_EXE=/path/BlueBrick.exe run-vanilla-bluebrick.sh <file.bbm>
USAGE
    exit 1
fi

BBM_ABS="$(readlink -f "$1")"
if [[ ! -f "${BBM_ABS}" ]]; then
    echo "not a file: ${BBM_ABS}" >&2
    exit 1
fi

# Locate BlueBrick.exe. User override wins; otherwise scan a couple of
# conventional install locations.
if [[ -z "${BLUEBRICK_EXE:-}" ]]; then
    for candidate in \
        "${HOME}/Documents/BlueBrick.1.9.2/BlueBrick.exe" \
        "${HOME}/Applications/BlueBrick.1.9.2/BlueBrick.exe" \
        "/opt/BlueBrick.1.9.2/BlueBrick.exe"; do
        if [[ -f "${candidate}" ]]; then
            BLUEBRICK_EXE="${candidate}"
            break
        fi
    done
fi
if [[ -z "${BLUEBRICK_EXE:-}" || ! -f "${BLUEBRICK_EXE}" ]]; then
    echo "BlueBrick.exe not found. Set BLUEBRICK_EXE=/path/BlueBrick.exe." >&2
    exit 1
fi

echo "BlueBrick.exe: ${BLUEBRICK_EXE}"
echo "Opening: ${BBM_ABS}"

if [[ -n "${PROTON:-}" && "${PROTON}" != "0" ]]; then
    # Proton-GE path. The shipped `proton` script is Python and
    # expects a handful of env vars; we fake them minimally so it
    # can launch a stand-alone Windows app without Steam.
    PROTON_DIR="/usr/share/steam/compatibilitytools.d/proton-ge-custom"
    if [[ ! -x "${PROTON_DIR}/proton" ]]; then
        echo "Proton-GE not found at ${PROTON_DIR}." >&2
        echo "Falling back to wine." >&2
        PROTON=""
    else
        export STEAM_COMPAT_CLIENT_INSTALL_PATH="${HOME}/.steam/steam"
        export STEAM_COMPAT_DATA_PATH="${HOME}/.local/share/proton-pfx/bluebrick"
        mkdir -p "${STEAM_COMPAT_DATA_PATH}"
        exec "${PROTON_DIR}/proton" run "${BLUEBRICK_EXE}" "${BBM_ABS}"
    fi
fi

# Default path: plain wine. The `wine start /unix` form resolves the
# path correctly inside the prefix and returns once BlueBrick is up.
exec wine "${BLUEBRICK_EXE}" "${BBM_ABS}"
