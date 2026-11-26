#!/usr/bin/env bash
# The gates, in order, with one timed summary (docs/WORKFLOW.md):
#   LEVEL=commit (make gate): the core build, ctest (RelWithDebInfo, build-core),
#     the extension build, the Godot host's suite, the goldens unchanged. The
#     result cache (tools/gate_cache.py): a ctest entry or the Godot suite whose
#     inputs are the same as when it last passed is not run again (GATE_CACHE=0:
#     run everything).
#   LEVEL=full (make gates-full, Stage 68): the merge gate with every sample of the truth's comparisons (LAB_SPARSE=1;
#     the others take every third) and every entry under the sanitizers (the others, those of 30 s or less in the
#     release tree and 20 s or less under the sanitizers: ASAN_MOST_S, ASAN_OWN_MOST_S).
#   LEVEL=merge (make gates): the same, and ctest under ASan+UBSan (build-asan),
#     side by side with the rest from the start (Stage 40: every ctest entry a test
#     case, the machine kept full; docs/TDD_40_GATES.md). Its cache takes only
#     what passed at a merge gate (Stage 68): run again after a failure, it runs
#     what failed and what changed.
#   SKIP_GODOT=1: every engine-free gate (no extension build, no Godot suite).
# Every gate runs even after a failure; the exit status is nonzero if any failed.
# Logs: $LOGDIR (printed).
#
#   LEVEL=commit ./gates.sh     # or: make gate
#   LEVEL=merge ./gates.sh      # or: make gates
#   LEVEL=full ./gates.sh       # or: make gates-full
#   SKIP_GODOT=1 make gate
set -uo pipefail
cd "$(dirname "$0")"
LEVEL="${LEVEL:-merge}"
LOGDIR=$(mktemp -d "${TMPDIR:-/tmp}/chem_gates.XXXXXX")
declare -a NAMES SECS RESULTS
status=0

gate() {
	local name=$1
	shift
	local t0=$(date +%s)
	echo "== $name"
	if "$@" > "$LOGDIR/$name.log" 2>&1; then
		RESULTS+=("ok")
	else
		RESULTS+=("FAIL")
		status=1
		tail -30 "$LOGDIR/$name.log"
	fi
	NAMES+=("$name")
	SECS+=($(( $(date +%s) - t0 )))
}

# a gate in the background (gate_join collects it, in the summary's order)
declare -a BG_NAMES BG_PIDS BG_T0
gate_bg() {
	local name=$1
	shift
	echo "== $name (in the background)"
	("$@" > "$LOGDIR/$name.log" 2>&1; rc=$?; date +%s > "$LOGDIR/$name.end"; exit $rc) &
	BG_NAMES+=("$name")
	BG_PIDS+=($!)
	BG_T0+=($(date +%s))
}
gate_join() {
	local i
	for i in "${!BG_PIDS[@]}"; do
		if wait "${BG_PIDS[$i]}"; then
			RESULTS+=("ok")
		else
			RESULTS+=("FAIL")
			status=1
			tail -30 "$LOGDIR/${BG_NAMES[$i]}.log"
		fi
		NAMES+=("${BG_NAMES[$i]}")
		SECS+=($(( $(cat "$LOGDIR/${BG_NAMES[$i]}.end" 2>/dev/null || date +%s) - BG_T0[i] )))
	done
	BG_NAMES=() BG_PIDS=() BG_T0=()
}

# the result cache at the gate's level (tools/gate_cache.py, Stage 68): a merge gate takes only what passed at a merge
# gate (a full gate's passes are merge passes); GATE_CACHE=0 runs everything
export GATE_LEVEL=$LEVEL
[ "$LEVEL" = full ] && export GATE_LEVEL=merge
# (Stage 68) how the gates sample the truth's heaviest comparisons (LAB_SPARSE: every third sample, as the sanitizer
# build always has; a full gate, every one) and which entries the sanitizers run (those of 30 s or less in the
# release tree and 20 s or less under the sanitizers, ASAN_MOST_S and ASAN_OWN_MOST_S; a full gate, all)
if [ "$LEVEL" = full ]; then
	export GATE_SPARSE="${GATE_SPARSE:-1}" ASAN_MOST_S="${ASAN_MOST_S:-0}" ASAN_OWN_MOST_S="${ASAN_OWN_MOST_S:-0}"
else
	export GATE_SPARSE="${GATE_SPARSE:-3}"
fi
cached() {
	python3 -I tools/gate_cache.py "$@"
}

# a changed golden is a behaviour change: judged and approved, then committed
goldens_unchanged() {
	local d
	d=$(git diff --stat -- bench)
	[ -z "$d" ] || { echo "$d"; return 1; }
}

# the Godot extension, then the Godot host's suite. The commit level keys the
# suite on the host (its project, its adapter, chem_api.h, the data, Godot), not
# on the game inside the extension: a game-only change is checked by the C++
# hosts on the C API at a commit, by every host at a merge.
godot_gate() {
	make native || return 1
	local godot_bin
	godot_bin=$(command -v "${GODOT_BIN:-godot}" || true)
	cached step gut --inputs engines/godot native/hosts/godot native/capi/include run_tests_parallel.sh data bench \
		${godot_bin:+"file:$godot_bin"} -- env SKIP_BUILD=1 ./run_tests_parallel.sh
}

wall0=$(date +%s)
# the sanitizers' build and run, from the start, beside everything else
if [ "$LEVEL" != commit ]; then
	gate_bg asan make asan-gate
fi
gate core make core
# side by side: ctest, the Godot extension and suite
gate_bg ctest env LAB_SPARSE="$GATE_SPARSE" python3 -I tools/gate_cache.py ctest native/build-core
if [ -z "${SKIP_GODOT:-}" ]; then
	gate_bg godot godot_gate
fi
gate_join
gate goldens goldens_unchanged

echo
printf "%-10s %6s  %s\n" gate secs result
for i in "${!NAMES[@]}"; do
	printf "%-10s %6d  %s\n" "${NAMES[$i]}" "${SECS[$i]}" "${RESULTS[$i]}"
done
grep -h "Passing (\|gate_cache:" "$LOGDIR"/{ctest,asan,godot}.log 2>/dev/null
# what the time went to (Stage 68): each ctest run's summed seconds and its slowest entries, so a slow new test is
# seen at its first gate
for run in ctest asan; do
	[ -s "$LOGDIR/$run.log" ] || continue
	sed -nE 's/.*Test +#[0-9]+: ([^ ]+) .* ([0-9.]+) sec$/\2 \1/p' "$LOGDIR/$run.log" | sort -rn > "$LOGDIR/$run.times"
	[ -s "$LOGDIR/$run.times" ] || continue
	printf "%s: %d entries, %d s summed; the slowest:" "$run" "$(wc -l < "$LOGDIR/$run.times")" "$(awk '{s += $1} END {printf "%d", s}' "$LOGDIR/$run.times")"
	head -"${GATE_SLOWEST:-8}" "$LOGDIR/$run.times" | awk '{printf " %s %ds,", $2, $1}' | sed 's/,$//'
	echo
done
# (Stage 68) the release run's budget: an entry over GATE_BUDGET_S seconds is named (it does not fail the gate: a
# machine's load moves the times). A new long play belongs with what makes the long ones short (docs/WORKFLOW.md,
# "What keeps the gates quick"): a fixture, parts, sparse(), coarse frames where only its end is held.
if [ "$LEVEL" != full ] && [ -s "$LOGDIR/ctest.times" ]; then # (a full gate takes every sample: its times are longer by design)
	over=$(awk -v most="${GATE_BUDGET_S:-60}" '$1 > most {printf " %s %ds,", $2, $1}' "$LOGDIR/ctest.times" | sed 's/,$//')
	[ -n "$over" ] && echo "!! over the budget of ${GATE_BUDGET_S:-60} s an entry:$over"
fi
printf "%-10s %6d  %s   (wall; LEVEL=%s%s, LAB_SPARSE=$GATE_SPARSE, logs: %s)\n" total "$(( $(date +%s) - wall0 ))" "$([ $status = 0 ] && echo ok || echo FAIL)" "$LEVEL" "${SKIP_GODOT:+, SKIP_GODOT}" "$LOGDIR"
exit $status
