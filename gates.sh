#!/usr/bin/env bash
# The gates, in order, with one timed summary (docs/WORKFLOW.md):
#   LEVEL=commit (make gate): the core build, ctest (RelWithDebInfo, build-core),
#     the extension build, the Godot host's suite, the goldens unchanged. The
#     result cache (tools/gate_cache.py): a ctest entry or the Godot suite whose
#     inputs are the same as when it last passed is not run again (GATE_CACHE=0:
#     run everything).
#   LEVEL=merge (make gates): the same with no cache, and ctest under
#     ASan+UBSan (build-asan), side by side with the rest from the start (Stage
#     40: every ctest entry a test case, the machine kept full; docs/TDD_40_GATES.md).
#   SKIP_GODOT=1: every engine-free gate (no extension build, no Godot suite).
# Every gate runs even after a failure; the exit status is nonzero if any failed.
# Logs: $LOGDIR (printed).
#
#   LEVEL=commit ./gates.sh     # or: make gate
#   LEVEL=merge ./gates.sh      # or: make gates
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

# the result cache: at the commit level only (tools/gate_cache.py)
cached() {
	if [ "$LEVEL" = commit ]; then
		python3 -I tools/gate_cache.py "$@"
	else
		GATE_CACHE=0 python3 -I tools/gate_cache.py "$@"
	fi
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
if [ "$LEVEL" = merge ]; then
	gate_bg asan make native-asan
fi
gate core make core
# side by side: ctest, the Godot extension and suite
if [ "$LEVEL" = commit ]; then
	gate_bg ctest python3 -I tools/gate_cache.py ctest native/build-core
else
	gate_bg ctest make ctest
fi
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
grep -h "Passing (\|gate_cache:" "$LOGDIR"/{ctest,godot}.log 2>/dev/null
printf "%-10s %6d  %s   (wall; LEVEL=%s%s, logs: %s)\n" total "$(( $(date +%s) - wall0 ))" "$([ $status = 0 ] && echo ok || echo FAIL)" "$LEVEL" "${SKIP_GODOT:+, SKIP_GODOT}" "$LOGDIR"
exit $status
