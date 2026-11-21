#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Runs make targets (default: check) in Alpine 3.24 with CI's packages (test/Dockerfile),
# this repository at /src and GlassineOS/ui (test/ui.sh) at /ui, read-only. out/ is given
# back to the caller's user afterwards.
#   test/in-alpine.sh [TARGET...]
set -eu
R=$(cd "$(dirname "$0")/.." && pwd)
UI=$(sh "$R/test/ui.sh")
IMG=$(docker build -q -t glassine-planner-spike "$R/test")
docker run --rm -v "$R":/src -v "$UI":/ui:ro -e GLASSINE_UI=/ui -e OWNER="$(id -u):$(id -g)" -w /src "$IMG" \
    sh -c 'make "$@"; s=$?; chown -R "$OWNER" out; exit $s' make "${@:-check}"
