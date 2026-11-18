#!/bin/bash
# Boot proof for the AMPR zstd-packed Astro Bot dump (and the old .zar regression).
# Usage: bash tools/local/test_ampr_boot.sh [GAME_PATH] [OUT_DIR]
set -u
cd "$(dirname "$0")/../.."

GAME=${1:-"/home/reng/disk/Downloads/[ASTRO BOT]-[PPSA21567]"}
OUT=${2:-/tmp/opencode/ampr-boot}
PATCH=${PATCH-docs/superpowers/astro-10190000-patches.json}  # set PATCH= (empty) to boot an older/unpatched dump
EMU=./_Build/linux/kyty_emulator

set -a
source <(python3 -c "
import json
d = json.load(open('/tmp/opencode/u59-preset.json'))
for k, v in d.items():
    if v == '' or v is None: continue
    print(f'{k}={v}')
")
set +a

rm -rf "$OUT"; mkdir -p "$OUT"
LOG="$OUT/run.log"

ARGS=(--game "$GAME")
[ -n "$PATCH" ] && ARGS+=(--game-patch "$PATCH")
ARGS+=(--screen-width 1600 --screen-height 900 \
  --console-language 1 --vblank-frequency 30 \
  --present-mode Mailbox --skip-notice-screen true --no-splash \
  --printf-direction Console)

KYTY_GAME="$GAME" "$EMU" "${ARGS[@]}" > "$LOG" 2>&1 &
EMU_PID=$!
trap 'kill "$EMU_PID" 2>/dev/null; wait "$EMU_PID" 2>/dev/null' EXIT

WIN=""
for i in $(seq 1 90); do
  for pat in PPSA21567 ASTRO kyty; do
    WIN=$(xdotool search --name "$pat" 2>/dev/null | head -1)
    [ -n "$WIN" ] && break
  done
  [ -n "$WIN" ] && break
  sleep 1
done
echo "game=$GAME"
echo "emulator_pid=$EMU_PID window=$WIN"
[ -n "$WIN" ] && xdotool windowactivate "$WIN" 2>/dev/null

for T in 30 60 90; do
  while [ "$SECONDS" -lt "$T" ]; do sleep 1; done
  if [ -n "$WIN" ]; then
    import -window "$WIN" "$OUT/frame_${T}s.png" 2>/dev/null
    if [ -s "$OUT/frame_${T}s.png" ]; then
      echo "${T}s: sha256=$(sha256sum "$OUT/frame_${T}s.png" | cut -c1-16) size=$(stat -c%s "$OUT/frame_${T}s.png")B"
    else
      echo "${T}s: capture FAILED"
    fi
  fi
done

echo "=== fatal/device-loss ==="
FATAL=$(grep -ciE 'Fatal|device lost' "$LOG")
DEVLOSS=$(grep -ciE 'device lost' "$LOG")
echo "combined=$FATAL device_lost=$DEVLOSS"
grep -aiE 'Fatal|device lost' "$LOG" | tail -5

echo "=== GoldHEN patch matches ==="
grep -a 'Game cheat: matched' "$LOG" | tail -10

echo "=== DrawRun stats ==="
awk -F: '/DrawRun/ {split($2,a," "); s+=a[1]; n++; if(a[1]>m)m=a[1]} END{printf "samples=%d cumulative_draws=%d max_per_interval=%d\n", n, s, m}' "$LOG"
grep -a 'DrawRun' "$LOG" | tail -2
