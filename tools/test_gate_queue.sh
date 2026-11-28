#!/usr/bin/env bash
# The gates' queue held to what it says (tools/gate_queue.sh; ctest `gate_queue`): runs asked for a second apart
# go one after the other in the order asked; runs asked for at once never overlap; a dead run's ticket holds
# nobody; the status is the command's; a run inside a run goes straight through; the wait is bounded; no ticket
# is left behind. Sleeps, no work: about 25 s of the clock.
set -uo pipefail
q="$(cd "$(dirname "$0")" && pwd)/gate_queue.sh"
dir=$(mktemp -d "${TMPDIR:-/tmp}/gate_queue_test.XXXXXX")
trap 'rm -rf "$dir"' EXIT
unset GATE_QUEUE_HELD GATE_QUEUE GATE_QUEUE_WAIT_S # (a gate runs this held)
export GATE_QUEUE_DIR="$dir/queue" GATE_QUEUE_POLL_S=1
log="$dir/log"
failed=0
say() {
	if [ "$1" = 0 ]; then echo "ok: $2"; else
		echo "FAIL: $2"
		failed=1
	fi
}
run() { "$q" bash -c "echo start $1 >> $log; sleep 2; echo end $1 >> $log" > "$dir/$1.out"; }

run a &
sleep 1
run b &
sleep 1
run c &
wait
[ "$(tr '\n' ' ' < "$log")" = "start a end a start b end b start c end c " ]
say $? "one at a time, in the order asked ($(tr '\n' ' ' < "$log"))"
grep -q "waiting behind" "$dir/b.out"
say $? "a waiting run says whom it waits behind"

: > "$log"
run d &
run e &
run f &
wait
awk '{ if ($1 == "start") { if (open) bad = 1; open = 1 } else open = 0 } END { exit bad || NR != 6 }' "$log"
say $? "three asked for at once never overlap ($(tr '\n' ' ' < "$log"))"

printf '999999\n/nowhere\na dead run\n' > "$GATE_QUEUE_DIR/0000000001-0999999"
t0=$(date +%s)
"$q" true
[ $(($(date +%s) - t0)) -le 3 ] && [ ! -e "$GATE_QUEUE_DIR/0000000001-0999999" ]
say $? "a dead run's ticket is swept and holds nobody"

"$q" bash -c 'exit 7'
[ $? -eq 7 ]
say $? "the command's status"

t0=$(date +%s)
"$q" "$q" "$q" true
[ $(($(date +%s) - t0)) -le 3 ]
say $? "a run inside a run goes straight through"

[ "$(GATE_QUEUE=0 "$q" bash -c 'echo ${GATE_QUEUE_HELD:-}')" = 1 ] && [ -z "$(ls "$GATE_QUEUE_DIR")" ]
say $? "GATE_QUEUE=0 runs with no ticket, and what it runs asks no more"

("$q" sleep 6) &
sleep 2
t0=$(date +%s)
GATE_QUEUE_WAIT_S=2 "$q" true > "$dir/bound.out"
[ $(($(date +%s) - t0)) -le 5 ] && grep -q "going on beside" "$dir/bound.out"
say $? "it stops waiting at its limit, and says so"
wait

[ -z "$(ls -A "$GATE_QUEUE_DIR")" ]
say $? "no ticket left behind"
exit $failed
