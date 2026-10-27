#!/usr/bin/env bash
# A series of deck.sh runs, one line each: deck-series.sh "label|deckbench options" ... (each a separate boot).
# Example: deck-series.sh "dev1|" "wave1|--set KYTY_COMPUTE_WAVE64=1"
set -uo pipefail
here="$(dirname "$(readlink -f "$0")")"
for spec in "$@"; do
	label="${spec%%|*}"
	options="${spec#*|}"
	result="$(bash "$here/deck.sh" run --label "$label" $options 2>&1 | grep '^RESULT ' | sed 's/^RESULT //')"
	if [ -z "$result" ]; then
		echo "$label: no result"
		continue
	fi
	python -c "
import json, sys
r = json.loads(sys.argv[1])
def line(m):
    return ('{fps:5.1f} fps render {render_ms:6.1f} ms busy {render_busy:4.0f}% p99 {p99_ms:6.1f} max {max_ms:7.1f} | gpu {gpu_busy}% {gpu_mhz} MHz '
            '{power_w} W cpu {cpu_mhz} MHz | mem {mem_avail_min_mb} MB free, swap {swap_used_max_mb} MB').format(**m)
out = ['%s (boot %ss)%s' % (r['label'], r.get('boot_s', '?'), ' ' + r['error'] if 'error' in r else '')]
for key in ('stand', 'spin'):
    if key in r: out.append('  %-5s %s' % (key, line(r[key])))
print('\n'.join(out))
" "$result"
done
