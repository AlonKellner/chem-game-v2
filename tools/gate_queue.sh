#!/usr/bin/env bash
# The gates' queue (docs/WORKFLOW.md, "One gate at a time"): a gate fills every core, and three sessions' gates
# side by side took three times as long each (a load of 60 on 12 cores, 9 minutes become 29). So a heavy run takes
# a ticket in a queue every worktree shares and waits its turn; they run in the order they asked.
#
#   tools/gate_queue.sh COMMAND...      # COMMAND when no earlier ticket's run is alive; its exit status
#
# A ticket is a file in $GATE_QUEUE_DIR (~/Library/Caches/chem-game-v2/gate_queue), named by the second it was
# taken and its process, holding that process, its directory and its command. A ticket whose process is gone is
# swept by whoever looks next, so a killed run holds nobody. A run inside a run (GATE_QUEUE_HELD) goes straight
# through. GATE_QUEUE=0: no queue. After GATE_QUEUE_WAIT_S seconds (an hour) it stops waiting and runs beside what
# is there, saying so: the wait is bounded.
set -uo pipefail
[ $# -eq 0 ] && exit 0
if [ "${GATE_QUEUE:-1}" = 0 ] || [ -n "${GATE_QUEUE_HELD:-}" ]; then
	export GATE_QUEUE_HELD=1 # (what it runs asks no more: gates.sh comes here once)
	exec "$@"
fi
dir="${GATE_QUEUE_DIR:-$HOME/Library/Caches/chem-game-v2/gate_queue}"
if ! mkdir -p "$dir" 2> /dev/null; then
	export GATE_QUEUE_HELD=1
	exec "$@" # (nowhere to queue: as before)
fi
ticket="$dir/$(date +%s)-$(printf '%07d' $$)"
# (written whole, then named: a ticket half written would read as a dead run's and be swept)
printf '%s\n%s\n%s\n' "$$" "$PWD" "$*" > "$dir/.$$.tmp" && mv "$dir/.$$.tmp" "$ticket"
trap 'rm -f "$ticket"' EXIT
trap 'rm -f "$ticket"; exit 130' INT TERM HUP

t0=$(date +%s)
said=""
# Two tickets taken in one second are ordered by their processes, and the later process may have looked before the
# earlier wrote: a second's pause, and every ticket of this one's second is there to be seen.
sleep 1
while :; do
	ahead=""
	for name in $(ls "$dir" 2> /dev/null | sort); do
		[ "$dir/$name" = "$ticket" ] && break
		pid=$(sed -n 1p "$dir/$name" 2> /dev/null)
		if [ -n "$pid" ] && kill -0 "$pid" 2> /dev/null; then
			ahead="$ahead $name"
		else
			rm -f "$dir/$name" # (its run is gone)
		fi
	done
	[ -z "$ahead" ] && break
	if [ "$ahead" != "$said" ]; then
		echo "gate queue: waiting behind"
		for name in $ahead; do
			at=${name%%-*} # (BSD's date reads seconds with -r, GNU's with -d @)
			since=$(date -r "$at" +%H:%M:%S 2> /dev/null || date -d "@$at" +%H:%M:%S 2> /dev/null)
			echo "   $(sed -n 3p "$dir/$name" 2> /dev/null | cut -c1-120) in $(sed -n 2p "$dir/$name" 2> /dev/null), asked at $since"
		done
		said=$ahead
	fi
	waited=$(($(date +%s) - t0))
	if [ "$waited" -ge "${GATE_QUEUE_WAIT_S:-3600}" ]; then
		echo "gate queue: waited $waited s; going on beside what is running"
		break
	fi
	sleep "${GATE_QUEUE_POLL_S:-5}"
done
waited=$(($(date +%s) - t0))
[ "$waited" -gt 1 ] && echo "gate queue: waited $waited s for its turn"
export GATE_QUEUE_HELD=1
"$@"
exit $?
