# The packages of CI's run (.github/workflows/build.yml), as an image for test/in-alpine.sh.
FROM alpine:3.24
RUN apk add --no-cache build-base pkgconf linux-headers git curl cairo-dev pango-dev librsvg-dev glib-dev \
    python3 py3-gobject3 py3-cairo gtk+3.0 font-dejavu font-eb-garamond adwaita-icon-theme \
    wayland-dev wayland-protocols libxkbcommon-dev sway grim wtype wlr-protocols fontconfig >/dev/null
