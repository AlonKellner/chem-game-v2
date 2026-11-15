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

.PHONY: core check quick gate-clear-cache native native-debug native-asan ctest gates gate test run godot-data

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

# The Godot project reads only res://: the data is copied in (gitignored).
godot-data:
	mkdir -p $(GODOT_PROJECT)/data
	rsync -a --delete data/ $(GODOT_PROJECT)/data/

# Debug extension (CHEM_ASSERT aborts). Writes the same library name, so it
# replaces the RelWithDebInfo one until the next `make native`.
native-debug:
	$(CMAKE) -S native -B native/build-debug $(call gen,native/build-debug) -DCMAKE_BUILD_TYPE=Debug \
		-DCHEM_BUILD_EXTENSION=ON -DCHEM_BUILD_TESTS=OFF -DCHEM_EXTENSION_OUTPUT_DIR=$(CURDIR)/$(GODOT_PROJECT)/bin
	$(CMAKE) --build native/build-debug -j $(JOBS_EXT)
	@$(MAKE) -s godot-data
	$(GODOT) --headless --import --path $(GODOT_PROJECT)

# The tests under ASan+UBSan (no godot-cpp), Debug (assertions abort) at -O1.
native-asan:
	$(CMAKE) -S native -B $(BUILD_ASAN) $(call gen,$(BUILD_ASAN)) -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS_DEBUG="-g -O1" -DCMAKE_C_FLAGS_DEBUG="-g -O1" \
		-DCHEM_SANITIZE=address,undefined -DCHEM_BUILD_EXTENSION=OFF -DCHEM_BUILD_TESTS=ON
	$(CMAKE) --build $(BUILD_ASAN) -j $(JOBS)
	$(CTEST) --test-dir $(BUILD_ASAN) -j $(CTEST_JOBS) --output-on-failure

# the release ctest (build-core; `make core` first)
ctest:
	$(CTEST) --test-dir $(BUILD_CORE) -j $(CTEST_JOBS) --output-on-failure

# Every gate before a merge or a stage close (gates.sh LEVEL=merge), with a timed
# summary; `make gate` is the commit gate (LEVEL=commit).
gates:
	LEVEL=merge ./gates.sh
gate:
	LEVEL=commit ./gates.sh

# ------------------------------------------------------------------ the Godot host
test: native
	SKIP_BUILD=1 ./run_tests_parallel.sh

run:
	$(GODOT) --path $(GODOT_PROJECT)
