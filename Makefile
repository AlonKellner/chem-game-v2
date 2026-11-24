GODOT ?= $(if $(GODOT_BIN),$(GODOT_BIN),godot)
GODOT_PROJECT := engines/godot
CMAKE ?= cmake
CTEST ?= ctest
NCPU := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
# compiles: every core, except a cold godot-cpp build (about 1100 TUs; an
# unbounded -j swaps): 4 until its library exists
JOBS ?= $(NCPU)
GODOTCPP_LIB := $(wildcard native/build/bin/libgodot-cpp*.a)
JOBS_EXT ?= $(if $(GODOTCPP_LIB),$(JOBS),4)
CTEST_JOBS ?= $(NCPU)
# a tree configured for the first time gets Ninja when installed
NINJA := $(shell command -v ninja 2>/dev/null)
gen = $(if $(wildcard $(1)/CMakeCache.txt),,$(if $(NINJA),-G Ninja,))

BUILD_CORE := native/build-core
BUILD_EXT := native/build
BUILD_ASAN := native/build-asan

.PHONY: pass-cost prism prism-shots core check quick gate-clear-cache native native-debug native-asan ctest gates gate test run godot-data demo demo-bench sessions sessions-check screenshots

# ---------------------------------------------------------------- native (C++)
# The three trees (docs/WORKFLOW.md):
#   build-core  the tests and chem_tool, no godot-cpp: the inner loop and every ctest gate
#   build       the GDExtension Godot loads (chem.gdextension -> engines/godot/bin), no tests
#   build-asan  the tests under ASan+UBSan, no godot-cpp

core:
	$(CMAKE) -S native -B $(BUILD_CORE) $(call gen,$(BUILD_CORE)) -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCHEM_BUILD_EXTENSION=OFF -DCHEM_BUILD_TESTS=ON
	$(CMAKE) --build $(BUILD_CORE) -j $(JOBS)

# The inner loop: core, then the ctest entries of an area, or one doctest filter.
#   make check                      # every entry
#   make check AREA=core|lab        # chem_tests | lab_tests and the C API
#   make check TC="conservation*"   # chem_tests -tc=<filter> (--no-skip with SKIP=0)
AREA ?= all
TC ?=
check_re_all := .
check_re_core := ^chem_
check_re_lab := ^lab|^capi
check: core
	@if [ -n "$(TC)" ]; then $(BUILD_CORE)/chem_tests -tc="$(TC)" $(if $(filter 0,$(SKIP)),--no-skip,); \
	else $(CTEST) --test-dir $(BUILD_CORE) -j $(CTEST_JOBS) --output-on-failure -R '$(check_re_$(AREA))'; fi

# Between commits: core, then every ctest entry a change can affect (the result
# cache, tools/gate_cache.py)
quick: core
	python3 -I tools/gate_cache.py ctest $(BUILD_CORE) --jobs $(CTEST_JOBS)
gate-clear-cache:
	python3 -I tools/gate_cache.py clear

# The extension (RelWithDebInfo) loaded by Godot. The headless --import makes a
# headless Godot discover it (.godot/extension_list.cfg); it runs when that file
# is missing or older than chem.gdextension.
native:
	$(CMAKE) -S native -B $(BUILD_EXT) $(call gen,$(BUILD_EXT)) -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCHEM_BUILD_EXTENSION=ON -DCHEM_BUILD_TESTS=OFF -DCHEM_EXTENSION_OUTPUT_DIR=$(CURDIR)/$(GODOT_PROJECT)/bin
	$(CMAKE) --build $(BUILD_EXT) -j $(JOBS_EXT)
	@$(MAKE) -s godot-data
	@if [ ! -s $(GODOT_PROJECT)/.godot/extension_list.cfg ] || [ $(GODOT_PROJECT)/chem.gdextension -nt $(GODOT_PROJECT)/.godot/extension_list.cfg ]; then \
		$(GODOT) --headless --import --path $(GODOT_PROJECT); else echo "import: extension_list.cfg current"; fi

# The Godot project reads only res://: the data is copied in (gitignored), and the
# per-pixel source.
godot-data:
	mkdir -p $(GODOT_PROJECT)/data
	rsync -a --delete data/ $(GODOT_PROJECT)/data/
	mkdir -p $(GODOT_PROJECT)/shaders
	cp native/game/shader/optics_pixel.glsl $(GODOT_PROJECT)/shaders/optics_pixel.glslinc

# Debug extension (CHEM_ASSERT aborts). Writes the same library name, so it
# replaces the RelWithDebInfo one until the next `make native`.
native-debug:
	$(CMAKE) -S native -B native/build-debug $(call gen,native/build-debug) -DCMAKE_BUILD_TYPE=Debug \
		-DCHEM_BUILD_EXTENSION=ON -DCHEM_BUILD_TESTS=OFF -DCHEM_EXTENSION_OUTPUT_DIR=$(CURDIR)/$(GODOT_PROJECT)/bin
	$(CMAKE) --build native/build-debug -j $(JOBS_EXT)
	@$(MAKE) -s godot-data
	$(GODOT) --headless --import --path $(GODOT_PROJECT)

# The tests under ASan+UBSan (no godot-cpp), Debug (assertions abort) at -O2: the
# instrumentation is the same, and the run 102 s against 237 s at -O1 (Stage 22).
# The conservation sweep runs ASAN_SWEEP_WORLDS random worlds here (the
# sanitizers want the code paths; the release ctest runs them all).
ASAN_SWEEP_WORLDS ?= 300
# and every third sample of the optics tests' heaviest comparisons against the truth (tests/support/sparse.hpp)
ASAN_SPARSE ?= 3
native-asan:
	$(CMAKE) -S native -B $(BUILD_ASAN) $(call gen,$(BUILD_ASAN)) -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS_DEBUG="-g -O2" -DCMAKE_C_FLAGS_DEBUG="-g -O2" \
		-DCHEM_SANITIZE=address,undefined -DCHEM_BUILD_EXTENSION=OFF -DCHEM_BUILD_TESTS=ON
	$(CMAKE) --build $(BUILD_ASAN) -j $(JOBS)
	CHEM_SWEEP_WORLDS=$(ASAN_SWEEP_WORLDS) LAB_SPARSE=$(ASAN_SPARSE) $(CTEST) --test-dir $(BUILD_ASAN) -j $(CTEST_JOBS) --output-on-failure

# the release ctest (build-core; `make core` first)
ctest:
	$(CTEST) --test-dir $(BUILD_CORE) -j $(CTEST_JOBS) --output-on-failure

# Every gate before a merge or a stage close (gates.sh LEVEL=merge), with a timed
# summary; `make gate` is the commit gate (LEVEL=commit).
gates:
	LEVEL=merge ./gates.sh
gate:
	LEVEL=commit ./gates.sh

# ------------------------------------------------------------------ sessions
# The 3-stage demo (GAME_SPEC §17), headless, through the C API: every command,
# then what was measured and the conservation totals.
demo: core
	$(BUILD_CORE)/chem_tool run data/sessions/demo.json
demo-bench: core
	$(BUILD_CORE)/chem_tool bench data/sessions/demo.json
# The sessions' golden records (bench/sessions/*.jsonl: the state's digest after
# every command). `sessions-check` replays against them (also ctest demo_session);
# `sessions` re-records them: a changed record is a behaviour change, to be shown
# and approved before it is committed.
sessions-check: core
	@for s in data/sessions/*.json; do $(BUILD_CORE)/chem_tool run $$s --quiet --check bench/sessions/$$(basename $$s .json).jsonl | tail -1 || exit 1; done
sessions: core
	@if [ "$(APPROVED)" != 1 ]; then echo "!! make sessions APPROVED=1: only after the user approved the change in behaviour"; exit 1; fi
	@for s in data/sessions/*.json; do $(BUILD_CORE)/chem_tool run $$s --quiet --record bench/sessions/$$(basename $$s .json).jsonl | tail -1 || exit 1; done

# ------------------------------------------------------------------ the Godot host
test: native
	SKIP_BUILD=1 ./run_tests_parallel.sh

# The prism demo (GAME_SPEC §12.3): a prism of flint glass before a slit of white light in a dark room
prism: native
	$(GODOT) --path $(GODOT_PROJECT) res://scenes/prism.tscn

# Three pictures of it -> build/screenshots/prism_*.png (headless first, then a window, time-limited)
prism-shots: native
	mkdir -p build/screenshots
	perl -e 'alarm 60; exec @ARGV' $(GODOT) --headless --path $(GODOT_PROJECT) -s tools/prism_shot.gd -- $(CURDIR)/build/screenshots
	perl -e 'alarm 60; exec @ARGV' $(GODOT) --path $(GODOT_PROJECT) -s tools/prism_shot.gd -- $(CURDIR)/build/screenshots

# The game.
run:
	$(GODOT) --path $(GODOT_PROJECT)

# The optical pass's cost on the GPU (Stage 33): a play (data/plays/cost.json: settled, a vessel carried, poured
# by hand, stirred), each mark's frames timed, then frames where every pixel's history starts again. Headless
# first (the play alone), then a window, time-limited.
PLAY ?= data/plays/cost.json
pass-cost: native
	perl -e 'alarm 60; exec @ARGV' $(GODOT) --headless --path $(GODOT_PROJECT) -s tools/play.gd -- $(CURDIR)/$(PLAY)
	perl -e 'alarm 150; exec @ARGV' $(GODOT) --disable-vsync --path $(GODOT_PROJECT) -s tools/play.gd -- $(CURDIR)/$(PLAY)

# Fourteen screenshots of the host through the demo -> build/screenshots; at each,
# the GPU's optical pass against the reference renderer (O4), and the GPU's
# time for a frame. It opens a window for a few seconds (a headless Godot
# paints nothing), played headless first and under a time limit: a script on
# the engine's thread must never be able to hang a window.
screenshots gpu-parity: native
	mkdir -p build/screenshots
	perl -e 'alarm 60; exec @ARGV' $(GODOT) --headless --path $(GODOT_PROJECT) -s tools/screenshot.gd -- $(CURDIR)/build/screenshots
	perl -e 'alarm 90; exec @ARGV' $(GODOT) --disable-vsync --path $(GODOT_PROJECT) -s tools/screenshot.gd -- $(CURDIR)/build/screenshots
