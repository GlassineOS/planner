#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Prints the path of a GlassineOS/handwriting checkout (the pen interface's client library,
# its client/), for a build without the installed library:
#   $GLASSINE_HW if set, else ../handwriting beside this repository, else a clone of
#   GlassineOS/handwriting at $GLASSINE_HW_REF (default: main) in out/hw-src.
set -eu
R=$(cd "$(dirname "$0")/.." && pwd)
if [ -n "${GLASSINE_HW:-}" ]; then
    [ -f "$GLASSINE_HW/client/glassine-handwriting.h" ] || { echo "GLASSINE_HW=$GLASSINE_HW: not a GlassineOS/handwriting checkout" >&2; exit 1; }
    cd "$GLASSINE_HW" && pwd
elif [ -f "$R/../handwriting/client/glassine-handwriting.h" ]; then
    cd "$R/../handwriting" && pwd
else
    C=$R/out/hw-src
    if [ ! -d "$C/.git" ]; then
        mkdir -p "$R/out"
        git clone -q https://github.com/GlassineOS/handwriting "$C" >&2
    fi
    ref=${GLASSINE_HW_REF:-main}
    git -C "$C" fetch -q --tags origin >&2
    git -C "$C" checkout -q --detach "origin/$ref" 2>/dev/null || git -C "$C" checkout -q --detach "$ref" >&2
    echo "$C"
fi
