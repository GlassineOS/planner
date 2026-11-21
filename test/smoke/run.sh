#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# The smoke run: glassine-planner under stock sway, headless (pixman), in portrait and
# landscape (scale 2, as the panel). A virtual pointer (vptr) stands in for the pen
# (left button) and a finger (right button), wtype for the keys; imwatch, an input method
# that only watches, logs when the system keyboard would be shown (text-input-v3).
#   1. a shot of every template in every voice: out/shots/TEMPLATE-VOICE-ORIENT.png
#   2. white, day-full: write a to-do, drag and stretch an event, draw a new one and write
#      its title, edit a reading in text mode, the debug overlay, switch through the day's
#      templates and back (the entries' file is byte-identical); shots on the way, the
#      ledger in out/shots/ledger-ORIENT.txt.
# The planner says where its targets are in its dump file (PLANNER_DUMP).
# Needs sway, grim, wtype; out/glassine-planner, out/vptr, out/imwatch (make smoke).
#   ORIENTS="portrait landscape" sh test/smoke/run.sh
set -u
R=$(cd "$(dirname "$0")/../.." && pwd)
O=$R/out/smoke SH=$R/out/shots B=$R/out
for c in sway grim wtype; do
    command -v $c >/dev/null || { echo "smoke: $c is missing (test/in-alpine.sh smoke runs it in Alpine)"; exit 1; }
done
rm -rf "$O" "$SH"
mkdir -p "$O" "$SH"
cp "$(command -v sway)" "$O/sway"   # the package's file capabilities: no exec in a container
fail=0
PIDS=
cleanup() { for p in $PIDS ${PL:-}; do kill $p 2>/dev/null; done; }
trap cleanup EXIT
ok() { echo "ok   $ORIENT: $1"; }
bad() { echo "FAIL $ORIENT: $1${2:+ ($2)}"; fail=1; }
expect() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1" "got: $2 | want: $3"; fi; }

# waits until grep -E PATTERN matches FILE (5 s)
waitfor() {
    i=0
    while [ $i -lt 50 ]; do grep -qE "$2" "$1" 2>/dev/null && return 0; sleep 0.1; i=$((i + 1)); done
    return 1
}
# a pointer command, and wait until vptr has sent it (it answers "done" per command)
ptr() {
    n=$(wc -l < "$VPO")
    echo "$*" > "$FIFO"
    i=0; while [ $i -lt 100 ] && [ "$(wc -l < "$VPO")" -le "$n" ]; do sleep 0.05; i=$((i + 1)); done
    sleep 0.15
}
shot() { sleep "${2:-0.6}"; grim -o HEADLESS-1 "$SH/$1.png"; }
dumpv() { awk -v k="$1" '$1 == k { $1 = ""; print substr($0, 2); exit }' "$DUMP"; }

# starts the planner: VOICE TEMPLATE VIEW (the demo data, fresh in $DATA)
planner() {
    [ -n "${PL:-}" ] && { kill $PL 2>/dev/null; wait $PL 2>/dev/null; }
    rm -f "$DUMP"
    : > "$LOG"
    PLANNER_TEMPLATES=$R/templates PLANNER_DATA=$DATA PLANNER_DEMO=1 PLANNER_TODAY=2031-03-12 \
        PLANNER_VOICE=$1 PLANNER_TEMPLATE=$2 PLANNER_VIEW=$3 PLANNER_DUMP=$DUMP PLANNER_LEDGER=1 \
        "$B/glassine-planner" >>"$LOG" 2>&1 &
    PL=$!
    waitfor "$DUMP" "^template $2\$" || bad "the planner shows $2" "no dump"
    # the pointer enters the new window before the first press (a move to where it already is
    # sends nothing)
    ptr "move 2 2"; ptr "move $((LW / 2)) $((LH / 2))"
}

# keys: a new virtual keyboard each time, so wait (-s) until the planner has its new
# wl_keyboard before the first key
# a hand-like stroke of loops from x over w px, at baseline y (stdout: vptr's words)
word() {
    awk -v x="$1" -v y="$2" -v w="$3" 'BEGIN { printf "stroke"; n = 24
        for (i = 0; i <= n; i++) { t = i / n; printf " %.1f %.1f", x + w * t, y - 7 * (1 - cos(t * 6.283 * 3)) / 2 }; print "" }'
}

session() {
    if [ "$ORIENT" = landscape ]; then RES=1872x1404 LW=936 LH=702; else RES=1404x1872 LW=702 LH=936; fi
    export XDG_RUNTIME_DIR=$O/rt-$ORIENT
    mkdir -p -m 700 "$XDG_RUNTIME_DIR"
    LOG=$O/planner-$ORIENT.log DUMP=$O/dump-$ORIENT IM=$O/im-$ORIENT.log FIFO=$XDG_RUNTIME_DIR/vptr
    : > "$LOG"
    printf 'output HEADLESS-1 resolution %s scale 2 bg #ffffff solid_color\ndefault_border none\ndefault_floating_border none\nfocus_follows_mouse no\n' "$RES" > "$O/sway-$ORIENT.conf"
    WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 "$O/sway" -c "$O/sway-$ORIENT.conf" >"$O/sway-$ORIENT.log" 2>&1 &
    SWAY=$!
    PIDS="$PIDS $SWAY"
    i=0; while [ $i -lt 50 ] && ! ls "$XDG_RUNTIME_DIR"/wayland-* >/dev/null 2>&1; do sleep 0.2; i=$((i + 1)); done
    export WAYLAND_DISPLAY=$(cd "$XDG_RUNTIME_DIR" && ls wayland-* | grep -v lock | head -1)
    export SWAYSOCK=$(ls "$XDG_RUNTIME_DIR"/sway-ipc.* 2>/dev/null | head -1)
    "$B/imwatch" "$IM" &
    IMW=$!
    PIDS="$PIDS $IMW"
    mkfifo "$FIFO"
    VPO=$O/vptr-$ORIENT.out; : > "$VPO"
    "$B/vptr" "$FIFO" $LW $LH >"$VPO" 2>"$O/vptr-$ORIENT.log" &
    VP=$!
    PIDS="$PIDS $VP"
    sleep 0.5

    # ---- 1. every template in every voice ---------------------------------------------------
    for v in ${VOICES-white editorial stationery}; do
        for t in day-full day-agenda day-lists week-columns week-focus; do
            case $t in week-*) view=week ;; *) view=day ;; esac
            DATA=$O/data-$ORIENT-shots; rm -rf "$DATA"
            planner $v $t $view
            shot "$t-$v-$ORIENT" 0.4
        done
    done
    [ -z "${VOICES+x}" ] && expect "15 shots of the templates" "$(ls "$SH" | grep -c -- "-$ORIENT.png")" 15

    # ---- 2. interactions, white, day-full ------------------------------------------------------
    DATA=$O/data-$ORIENT; rm -rf "$DATA"
    planner white day-full day
    E=$DATA/entries.txt
    expect "tablet-v2 and text-input-v3 found" "$(grep -c 'tablet-v2 yes, text-input-v3 yes' "$LOG")" 1
    todos() { grep -c '^entry [0-9]* to-do' "$E"; }
    n0=$(todos)

    # write two words into the empty row of today's list
    set -- $(awk '$1 == "field" && $2 == 0 && $3 == "to-do" { print $5, $6, $7, $8; exit }' "$DUMP") 0 0 0 0
    fx=$1 fy=$2 fh=$4
    rl=$(dumpv relayouts)
    ptr "$(word $((fx + 10)) $((fy + fh - 12)) 60)"
    ptr "$(word $((fx + 82)) $((fy + fh - 12)) 44)"
    expect "two strokes: one new to-do" "$(todos)" $((n0 + 1))
    expect "nothing relaid out while writing" "$(dumpv relayouts)" "$rl"
    grep -q 'planner: layout held' "$LOG" && ok "the layout change was held" || bad "the layout change was held"
    waitfor "$LOG" "action settle after pause" && ok "it settled after the pause" || bad "it settled after the pause"
    NEW=$(grep '^entry [0-9]* to-do' "$E" | tail -1 | cut -d' ' -f2)
    waitfor "$LOG" "#$NEW reading \(stub" && ok "the stand-in read it" || bad "the stand-in read it"
    expect "the new to-do's two strokes are stored" "$(awk -v id=$NEW '$1 == "entry" { on = ($2 == id) } on && $1 == "s"' "$E" | wc -l)" 2
    expect "a new empty row under it" "$(awk -v y=$fy '$1 == "field" && $2 == 0 && $3 == "to-do" && $6 > y' "$DUMP" | head -1 | wc -l)" 1
    shot "write-todo-white-$ORIENT"

    # drag the 9:00 event down by 96 minutes (lands on 10:30), stretch it by 40 (30)
    EV=$(awk '$1 == "entry" && $3 == "event" && $6 == "start=540" { print $2; exit }' "$E")
    PITCH=$(awk '$1 == "widget" && $3 == "agenda" { sub("pitch=", "", $10); print $10; exit }' "$DUMP")
    set -- $(awk -v id=$EV '$1 == "hit" && $2 == "ev-move" && $3 == id { print $5, $6, $7, $8; exit }' "$DUMP") 0 0 0 0
    x=$(($1 + $3 / 2)) y=$(($2 + $4 / 2))
    ptr "drag $x $y $x $(awk -v y=$y -v p=$PITCH 'BEGIN { printf "%.0f", y + p * 1.6 }') 12"
    sleep 0.3
    expect "the event moved in 30-minute steps" "$(awk -v id=$EV '$1 == "entry" && $2 == id { print $6, $7 }' "$E")" "start=630 end=690"
    set -- $(awk -v id=$EV '$1 == "hit" && $2 == "ev-size" && $3 == id { print $5, $6, $7, $8; exit }' "$DUMP") 0 0 0 0
    x=$(($1 + 30)) y=$(($2 + 6))
    ptr "drag $x $y $x $(awk -v y=$y -v p=$PITCH 'BEGIN { printf "%.0f", y + p * 0.67 }') 6"
    sleep 0.3
    expect "its lower edge stretched by one step" "$(awk -v id=$EV '$1 == "entry" && $2 == id { print $6, $7 }' "$E")" "start=630 end=720"
    grep 'commit event drag' "$LOG" > "$O/drag-$ORIENT.txt"
    expect "a redraw per 30-minute step of the drag (3) and one on release" "$(grep -c 'commit event drag' "$LOG")" 4

    # draw a block over 17:00-18:30 and write its title
    set -- $(awk '$1 == "hit" && $2 == "agenda" { print $5, $6, $7, $8; exit }' "$DUMP") 0 0 0 0
    FROM=$(awk '$1 == "widget" && $3 == "agenda" { sub("from=", "", $11); print $11; exit }' "$DUMP")
    x=$(($1 + 40)) y=$(awk -v y=$2 -v p=$PITCH -v f=$FROM 'BEGIN { printf "%.0f", y + (17 - f) * p + 3 }')
    ptr "drag $x $y $x $(awk -v y=$y -v p=$PITCH 'BEGIN { printf "%.0f", y + p * 1.4 }') 10"
    sleep 0.3
    BL=$(grep '^entry [0-9]* event' "$E" | tail -1)
    expect "a block drawn in free time: a new event" "$(echo "$BL" | cut -d' ' -f6,7)" "start=1020 end=1110"
    BID=$(echo "$BL" | cut -d' ' -f2)
    sleep 0.3
    set -- $(awk -v id=$BID '$1 == "field" && $2 == id { print $5, $6, $7, $8; exit }' "$DUMP") 0 0 0 0
    ptr "$(word $(($1 + 8)) $(($2 + 16)) 50)"
    sleep 1.2
    expect "its title's stroke is stored with the event" "$(awk -v id=$BID '$1 == "entry" { on = ($2 == id) } on && $1 == "s"' "$E" | wc -l)" 1
    shot "agenda-white-$ORIENT"

    # text mode: a finger (the right button) on the new to-do, type, enter
    set -- $(awk -v id=$NEW '$1 == "field" && $2 == id { print $5, $6, $7, $8; exit }' "$DUMP") 0 0 0 0
    ptr "rtap $(($1 + 40)) $(($2 + 12))"
    waitfor "$LOG" "text-input enabled" && ok "text mode enables text-input-v3" || bad "text mode enables text-input-v3"
    waitfor "$IM" "^activate" && ok "the input method is activated (the keyboard would show)" || bad "the input method is activated"
    shot "text-mode-white-$ORIENT"
    wtype -s 300 " soon"; sleep 0.5
    wtype -s 300 -k Return; sleep 0.6
    TXT=$(awk -v id=$NEW '$1 == "entry" { on = ($2 == id) } on && $1 == "text"' "$E")
    case "$TXT" in *" soon") ok "typed keys edit the reading: [$TXT]" ;; *) bad "typed keys edit the reading" "$TXT" ;; esac
    waitfor "$IM" "^deactivate" && ok "leaving text mode deactivates it" || bad "leaving text mode deactivates it"

    # the debug overlay: a stroke's way from ink to data
    wtype -s 300 -k F2; sleep 0.4
    set -- $(awk '$1 == "field" && $2 == 0 && $3 == "to-do" { print $5, $6, $7, $8; exit }' "$DUMP") 0 0 0 0
    ptr "$(word $(($1 + 10)) $(($2 + $4 - 12)) 70)"
    sleep 1.2
    shot "debug-white-$ORIENT"
    wtype -s 300 -k F2; sleep 0.4

    # every day template and back: the entries don't change
    cp "$E" "$O/entries-before-$ORIENT"
    tap_hit() { set -- $(awk -v a=$1 '$1 == "hit" && $2 == a { print $5, $6, $7, $8; exit }' "$DUMP") 0 0 0 0; ptr "tap $(($1 + $3 / 2)) $(($2 + $4 / 2))"; sleep 0.5; }
    seen=""
    for i in 1 2 3; do
        tap_hit template
        t=$(dumpv template); seen="$seen $t"
        [ "$t" = day-agenda ] && { expect "day-agenda: the notice counts the day's to-dos and note" "$(dumpv hidden)" \
            "0 $(grep -c '^entry [0-9]* to-do day 2031-03-12' "$E") 1"; shot "notice-white-$ORIENT"; }
    done
    expect "the switches went through every day template and back" "$seen" " day-lists day-agenda day-full"
    cmp -s "$E" "$O/entries-before-$ORIENT" && ok "switching templates left the entries' file byte-identical" ||
        bad "switching templates left the entries' file byte-identical"
    grep '^ledger' "$DUMP" | cut -d' ' -f2- > "$SH/ledger-$ORIENT.txt"
    grep '^planner: commit' "$LOG" | awk '{ a[$3" "$4]++ } END { for (k in a) print a[k], k }' | sort -k2 >> "$SH/ledger-$ORIENT.txt"

    kill $PL $VP $IMW 2>/dev/null
    kill $SWAY 2>/dev/null
    wait 2>/dev/null
    PL=
}

for ORIENT in ${ORIENTS:-portrait landscape}; do session; done
n=$(ls "$SH"/*.png 2>/dev/null | wc -l)
[ $fail = 0 ] && echo "smoke: all checks passed, $n shots in out/shots" || echo "smoke: FAILED, $n shots in out/shots (logs in out/smoke)"
exit $fail
