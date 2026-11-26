# chem-game-v2 native toolchain.
#
#   chem_core   static, engine-free (namespace chem): the simulation kernel   core/
#   chem_lab    static, engine-free (namespace lab): the lab layer            game/
#   chem_capi   the C API (chem_api.h), static and shared                     capi/
#   chem_gd     shared GDExtension: the Godot host on the C API               hosts/godot/ -> engines/godot/bin
#   chem_tests / lab_tests   doctest executables, a ctest entry a source file   tests/, tests/game/
#   chem_datagen  embeds data/ in chem_core, refusing unbalanced reactions       datagen/
#   chem_tool   the headless host                                             hosts/headless/
#
# Configure/build from the repo root (docs/WORKFLOW.md):
#   make core      # tests and tools alone (native/build-core, no godot-cpp)
#   make check     # core + ctest
#   make native    # the extension (native/build, no tests) for Godot
cmake_minimum_required(VERSION 3.25)
project(chem_native LANGUAGES CXX)

option(CHEM_BUILD_TESTS "Build the doctest executables and register ctest tests" ON)
option(CHEM_BUILD_EXTENSION "Build godot-cpp and the chem_gd GDExtension library" ON)
set(CHEM_SANITIZE "" CACHE STRING "Comma-separated -fsanitize= list for every target, e.g. address,undefined")
set(CHEM_EXTENSION_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../engines/godot/bin" CACHE PATH
    "Where the GDExtension library is written (engines/godot/chem.gdextension points at engines/godot/bin)")

# ccache when installed. Content-keyed, so a branch switch or a stash that
# restores a file compiles nothing twice. Stage 68: one cache for every worktree,
# the project's own (CHEM_CCACHE_DIR, its own size; the user's ccache untouched),
# with paths under the worktree's root made relative and the working directory
# left out (base_dir, hash_dir), so a worktree compiles what another already has.
get_filename_component(CHEM_WORKTREE_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
if(APPLE)
    set(CHEM_CACHE_HOME "$ENV{HOME}/Library/Caches/chem-game-v2")
elseif(DEFINED ENV{XDG_CACHE_HOME})
    set(CHEM_CACHE_HOME "$ENV{XDG_CACHE_HOME}/chem-game-v2")
else()
    set(CHEM_CACHE_HOME "$ENV{HOME}/.cache/chem-game-v2")
endif()
set(CHEM_CCACHE_DIR "${CHEM_CACHE_HOME}/ccache" CACHE PATH "the project's ccache, shared by its worktrees")
set(CHEM_CCACHE_SIZE "40G" CACHE STRING "the project's ccache's size")
option(CHEM_CCACHE_SHARED "compile through the project's shared ccache" ON)
find_program(CHEM_CCACHE ccache)
if(CHEM_CCACHE AND CHEM_CCACHE_SHARED)
    # (a normal variable, over a launcher an older configure left in the cache; the compiler's version in the key,
    # as /usr/bin/c++ is a shim whose mtime and content do not change with the compiler)
    foreach(lang CXX C)
        set(CMAKE_${lang}_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;CCACHE_DIR=${CHEM_CCACHE_DIR};CCACHE_MAXSIZE=${CHEM_CCACHE_SIZE};CCACHE_BASEDIR=${CHEM_WORKTREE_ROOT};CCACHE_NOHASHDIR=1;CCACHE_COMPILERCHECK=string:${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION};${CHEM_CCACHE}")
    endforeach()
elseif(CHEM_CCACHE AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CHEM_CCACHE}" CACHE STRING "compiler launcher (ccache)")
endif()

# A link is a function of its objects: macOS writes each object's mtime into the
# binary's debug map; zeroed, relinking the same objects gives the same bytes.
# The gates' result cache keys on the binaries (tools/gate_cache.py).
if(APPLE AND NOT DEFINED CMAKE_CXX_LINKER_LAUNCHER)
    set(CMAKE_CXX_LINKER_LAUNCHER "${CMAKE_COMMAND};-E;env;ZERO_AR_DATE=1" CACHE STRING "linker launcher (deterministic links)")
    set(CMAKE_C_LINKER_LAUNCHER "${CMAKE_COMMAND};-E;env;ZERO_AR_DATE=1" CACHE STRING "linker launcher (deterministic links)")
endif()
# And no path of the worktree in an object (Stage 68): the debug info's directory is "." (the sources' paths are
# relative, ccache's base_dir). A binary's debug map still names its objects by their whole paths: relative ones
# (-oso_prefix) left a sanitizer's report and atos without a line; the gates' cache reads a binary with its
# worktree's root left out (tools/gate_cache.py), so two worktrees on one commit still share their results.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fdebug-compilation-dir=.)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_property(CHEM_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT CHEM_IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Sanitizers apply to everything configured below this line, godot-cpp included,
# so instrumented and uninstrumented code never mix in one binary.
if(CHEM_SANITIZE)
    add_compile_options(-fsanitize=${CHEM_SANITIZE} -fno-omit-frame-pointer)
    add_compile_definitions(CHEM_SANITIZED=1) # costs are measured there, not bounded
    add_link_options(-fsanitize=${CHEM_SANITIZE})
endif()

include(cmake/fp.cmake)

# -------------------------------------------------------------------- chem_core
file(GLOB CHEM_CORE_SOURCES CONFIGURE_DEPENDS core/src/*.cpp)

# The game's data, embedded (data/elements.json, species.json, reactions.json):
# chem_datagen derives the element counts from the formulas and builds the tables
# through the core's own validation, so a reaction that does not balance in every
# element and in charge fails the build (GAME_SPEC §4.3). The generated tables
# repeat the proof as a static_assert.
get_filename_component(CHEM_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data" ABSOLUTE)
set(CHEM_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_executable(chem_datagen datagen/datagen.cpp core/src/species.cpp)
target_include_directories(chem_datagen PRIVATE core/include datagen)
target_include_directories(chem_datagen SYSTEM PRIVATE extern)
chem_apply_flags(chem_datagen)
add_custom_command(
    OUTPUT "${CHEM_GEN_DIR}/builtin_tables.cpp" "${CHEM_GEN_DIR}/builtin_optics.cpp" "${CHEM_GEN_DIR}/include/chem/builtin_ids.hpp"
    COMMAND chem_datagen "${CHEM_DATA_DIR}" "${CHEM_GEN_DIR}"
    DEPENDS chem_datagen "${CHEM_DATA_DIR}/elements.json" "${CHEM_DATA_DIR}/species.json" "${CHEM_DATA_DIR}/reactions.json" "${CHEM_DATA_DIR}/solutions.json"
            "${CHEM_DATA_DIR}/optics/media.json" "${CHEM_DATA_DIR}/optics/cie_1931_2deg.csv" "${CHEM_DATA_DIR}/optics/d65.csv"
    COMMENT "Embedding the species and reactions (data/), every reaction balanced")

add_library(chem_core STATIC ${CHEM_CORE_SOURCES} "${CHEM_GEN_DIR}/builtin_tables.cpp" "${CHEM_GEN_DIR}/builtin_optics.cpp")
target_include_directories(chem_core PUBLIC core/include "${CHEM_GEN_DIR}/include")
set_target_properties(chem_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# CHEM_ASSERT policy (core/include/chem/error.hpp): abort in Debug, report through
# the error sink and continue otherwise. PUBLIC so every consumer agrees with core.
target_compile_definitions(chem_core PUBLIC "CHEM_ASSERT_ABORTS=$<IF:$<CONFIG:Debug>,1,0>")
chem_apply_flags(chem_core)

# --------------------------------------------------------------------- chem_lab
# The glassware table (data/glassware.json, GAME_SPEC §12.3), embedded as text; read again when it changes.
file(READ "${CHEM_DATA_DIR}/glassware.json" CHEM_GLASSWARE_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CHEM_DATA_DIR}/glassware.json")
configure_file(game/glassware_data.hpp.in "${CHEM_GEN_DIR}/lab_include/lab/glassware_data.hpp" @ONLY)
# Boiling's bubbles' data (data/boiling.json, GAME_SPEC §11, Stage 45), embedded the same way.
file(READ "${CHEM_DATA_DIR}/boiling.json" CHEM_BOILING_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CHEM_DATA_DIR}/boiling.json")
configure_file(game/boiling_data.hpp.in "${CHEM_GEN_DIR}/lab_include/lab/boiling_data.hpp" @ONLY)
# A vessel's wall as rings (data/walls.json, GAME_SPEC §6, Stage 53), embedded the same way.
file(READ "${CHEM_DATA_DIR}/walls.json" CHEM_WALLS_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CHEM_DATA_DIR}/walls.json")
configure_file(game/walls_data.hpp.in "${CHEM_GEN_DIR}/lab_include/lab/walls_data.hpp" @ONLY)
# A thermal camera's palettes and gains (data/thermal.json, GAME_SPEC §13, Stage 48), embedded the same way.
file(READ "${CHEM_DATA_DIR}/thermal.json" CHEM_THERMAL_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CHEM_DATA_DIR}/thermal.json")
configure_file(game/thermal_data.hpp.in "${CHEM_GEN_DIR}/lab_include/lab/thermal_data.hpp" @ONLY)
# The instruments' resolutions and errors (data/instruments.json, GAME_SPEC §13, Readings; Stage 50), embedded the same way.
file(READ "${CHEM_DATA_DIR}/instruments.json" CHEM_INSTRUMENTS_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CHEM_DATA_DIR}/instruments.json")
configure_file(game/instruments_data.hpp.in "${CHEM_GEN_DIR}/lab_include/lab/instruments_data.hpp" @ONLY)
# Every shape of the glassware table made at build time (Stage 68): chem_glassgen compiles glassware.cpp as the lab
# does, makes each shape, and writes it out to the last bit; glass::find takes them (making them was more than half a
# short test's time, and the game's opening). test_glassware.cpp holds each to glass::make.
add_executable(chem_glassgen datagen/glassgen.cpp game/src/glassware.cpp)
target_include_directories(chem_glassgen PRIVATE game/include "${CHEM_GEN_DIR}/lab_include")
target_include_directories(chem_glassgen SYSTEM PRIVATE extern)
target_compile_definitions(chem_glassgen PRIVATE CHEM_GLASSGEN=1)
set_target_properties(chem_glassgen PROPERTIES POSITION_INDEPENDENT_CODE ON) # (compiled as chem_lab's)
chem_apply_flags(chem_glassgen)
set(CHEM_GLASSWARE_BUILT "${CHEM_GEN_DIR}/lab_include/lab/glassware_built.inc")
add_custom_command(
    OUTPUT "${CHEM_GLASSWARE_BUILT}"
    COMMAND chem_glassgen "${CHEM_GLASSWARE_BUILT}"
    DEPENDS chem_glassgen "${CHEM_DATA_DIR}/glassware.json"
    COMMENT "Making every shape of the glassware table (data/glassware.json)")
file(GLOB CHEM_LAB_SOURCES CONFIGURE_DEPENDS game/src/*.cpp)
add_library(chem_lab STATIC ${CHEM_LAB_SOURCES} "${CHEM_GLASSWARE_BUILT}")
target_include_directories(chem_lab PUBLIC game/include "${CHEM_GEN_DIR}/lab_include")
target_include_directories(chem_lab SYSTEM PUBLIC extern)
target_link_libraries(chem_lab PUBLIC chem_core)
set_target_properties(chem_lab PROPERTIES POSITION_INDEPENDENT_CODE ON)
chem_apply_flags(chem_lab)

# -------------------------------------------------------------------- chem_capi
# The game for any engine or host: static (chem_tool, the Godot adapter) and
# shared (C, ctypes, P/Invoke hosts).
file(GLOB CHEM_CAPI_SOURCES CONFIGURE_DEPENDS capi/src/*.cpp)
add_library(chem_capi STATIC ${CHEM_CAPI_SOURCES})
target_include_directories(chem_capi PUBLIC capi/include)
target_link_libraries(chem_capi PRIVATE chem_lab)
set_target_properties(chem_capi PROPERTIES POSITION_INDEPENDENT_CODE ON)
chem_apply_flags(chem_capi)
add_library(chem_capi_shared SHARED ${CHEM_CAPI_SOURCES})
target_include_directories(chem_capi_shared PUBLIC capi/include)
target_link_libraries(chem_capi_shared PRIVATE chem_lab)
set_target_properties(chem_capi_shared PROPERTIES OUTPUT_NAME chem_capi CXX_VISIBILITY_PRESET hidden)
chem_apply_flags(chem_capi_shared)

# the headless host, on the C API alone
file(GLOB CHEM_TOOL_SOURCES CONFIGURE_DEPENDS hosts/headless/*.cpp)
add_executable(chem_tool ${CHEM_TOOL_SOURCES})
target_link_libraries(chem_tool PRIVATE chem_capi chem_lab) # the studies drive the core directly
chem_apply_flags(chem_tool)
# (the studies that open the repository's benches and tutorials: chem_tool study speed) The repository's root found
# at run time, from where the binary is (Stage 68: tests/support/repo_root.hpp), so no flag names the worktree
function(chem_repo_root_at_run_time target)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/repo_root.cpp")
    target_compile_definitions(${target} PRIVATE "CHEM_REPO_ROOT=chem_repo_root()")
    target_compile_options(${target} PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/repo_root.hpp")
endfunction()
chem_repo_root_at_run_time(chem_tool)

# ---------------------------------------------------------------------- chem_gd
if(CHEM_BUILD_EXTENSION)
    # godot-cpp options (names from extern/godot-cpp/cmake/godotcpp.cmake).
    set(GODOTCPP_TARGET "template_debug" CACHE STRING "godot-cpp target: template_debug|template_release|editor")
    set(GODOTCPP_API_VERSION "4.7" CACHE STRING "Bundled extension_api-<ver>.json to generate bindings from")
    set(GODOTCPP_PRECISION "single" CACHE STRING "Must match the engine build (this project is single precision)")
    set(GODOTCPP_USE_HOT_RELOAD ON CACHE BOOL "hot reload support in the bindings (chem.gdextension has reloadable = false)")
    set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "godot-cpp headers are not held to the chem warning flags")
    add_subdirectory(extern/godot-cpp SYSTEM)

    file(GLOB CHEM_GD_SOURCES CONFIGURE_DEPENDS hosts/godot/*.cpp)
    add_library(chem_gd SHARED ${CHEM_GD_SOURCES})
    target_link_libraries(chem_gd PRIVATE chem_capi godot-cpp)

    get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
    set_target_properties(chem_gd PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON
        PREFIX "lib"
        OUTPUT_NAME "chem_gd${GODOTCPP_SUFFIX}"
        # the genex wrapper keeps multi-config generators from appending a config dir
        LIBRARY_OUTPUT_DIRECTORY "$<1:${CHEM_EXTENSION_OUTPUT_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${CHEM_EXTENSION_OUTPUT_DIR}>"
        ARCHIVE_OUTPUT_DIRECTORY "$<1:${CHEM_EXTENSION_OUTPUT_DIR}>"
        PDB_OUTPUT_DIRECTORY "$<1:${CHEM_EXTENSION_OUTPUT_DIR}>")
    # -Wconversion stays off here: godot-cpp's binding templates narrow freely
    # inside macros that expand in our translation units.
    chem_apply_flags(chem_gd NO_CONVERSION)
endif()

# ------------------------------------------------------------------------ tests
if(CHEM_BUILD_TESTS)
    enable_testing()
    get_filename_component(CHEM_REPO_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)

    # the core's tests
    file(GLOB CHEM_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
    add_executable(chem_tests ${CHEM_TEST_SOURCES})
    target_link_libraries(chem_tests PRIVATE chem_core)
    # vendored single headers: <doctest/doctest.h>, <nlohmann/json.hpp>
    target_include_directories(chem_tests SYSTEM PRIVATE extern)
    target_include_directories(chem_tests PRIVATE tests/support datagen)
    chem_repo_root_at_run_time(chem_tests)
    chem_apply_flags(chem_tests NO_CONVERSION)
    # One ctest entry for each source file of a test binary (its test cases, by doctest's source-file
    # filter): ctest runs them side by side, and the gate's cache keeps each apart. And one that fails if a
    # test case is in a file no entry runs.
    function(chem_add_tests target)
        set(all "")
        foreach(source ${ARGN})
            get_filename_component(stem ${source} NAME_WE)
            if(NOT stem STREQUAL "test_main")
                string(REGEX REPLACE "^test_" "" short ${stem})
                add_test(NAME ${target}.${short} COMMAND ${target} --source-file=*${stem}.cpp)
                list(APPEND all "*${stem}.cpp")
            endif()
        endforeach()
        list(JOIN all "," excluded)
        add_test(NAME ${target}.every_file_runs COMMAND ${target} --count --source-file-exclude=${excluded})
        set_tests_properties(${target}.every_file_runs PROPERTIES PASS_REGULAR_EXPRESSION "passing the current filters: 0")
    endfunction()
    # The same, a ctest entry for each test case (doctest's --first and --last within its file, counted from
    # the source's lines that start with TEST_CASE): the cases of a file run side by side, so no file is the
    # run's last minutes alone (the gates, Stage 40). Each case keeps to one thread (LAB_THREADS=1: side by
    # side the cases fill the machine, and a thread more a case mostly waits; docs/TDD_40_GATES.md). Marks on
    # the lines before a test case: `// ctest: parts N`, N entries, each with LAB_PART=i/N (the case does the
    # i-th share of its heaviest loop, tests/support/part.hpp; under the sanitizers only the first part runs, on
    # four threads);
    # `// ctest: threads N`, N threads, and ctest
    # counts it as N processors; `// ctest: fixture setup NAME`, the case makes the fixture NAME (its files in the
    # tree's fixtures/NAME, tests/support/fixtures.hpp), and `// ctest: fixture NAME`, the case starts from it: ctest
    # runs the setup first, and whenever a case that needs it runs (Stage 68: a simulation played once for the cases
    # that share it). Each file's count is checked against doctest's own (.count), each entry runs a
    # case (FAIL on none), and an edit to a file configures again (CMAKE_CONFIGURE_DEPENDS).
    function(chem_add_cases target)
        set(all "")
        foreach(source ${ARGN})
            get_filename_component(stem ${source} NAME_WE)
            if(stem STREQUAL "test_main")
                continue()
            endif()
            string(REGEX REPLACE "^test_" "" short ${stem})
            list(APPEND all "*${stem}.cpp")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
            file(STRINGS ${source} lines REGEX "^(TEST_CASE\\(|// ctest: (parts|threads) [0-9]+|// ctest: fixture )")
            set(case 0)
            set(parts 1)
            set(threads 1)
            set(fixture_setup "")
            set(fixture_needed "")
            foreach(line IN LISTS lines)
                if(line MATCHES "^// ctest: parts ([0-9]+)")
                    set(parts ${CMAKE_MATCH_1})
                elseif(line MATCHES "^// ctest: threads ([0-9]+)")
                    set(threads ${CMAKE_MATCH_1})
                elseif(line MATCHES "^// ctest: fixture setup ([a-z_0-9]+)")
                    set(fixture_setup ${CMAKE_MATCH_1})
                elseif(line MATCHES "^// ctest: fixture ([a-z_0-9]+)")
                    set(fixture_needed ${CMAKE_MATCH_1})
                elseif(line MATCHES "^TEST_CASE\\(")
                    math(EXPR case "${case} + 1")
                    math(EXPR last "${parts} - 1")
                    if(CHEM_SANITIZE AND parts GREATER 1)
                        # the sanitizers want the code's paths: a parted case's first part (a random share of its
                        # loop), on four threads
                        set(last 0)
                        set(threads 4)
                    endif()
                    foreach(part RANGE ${last})
                        if(parts GREATER 1)
                            set(name ${target}.${short}.${case}.${part})
                            set(environment "LAB_THREADS=${threads};LAB_PART=${part}/${parts}")
                        else()
                            set(name ${target}.${short}.${case})
                            set(environment "LAB_THREADS=${threads}")
                        endif()
                        add_test(NAME ${name} COMMAND ${target} --source-file=*${stem}.cpp --first=${case} --last=${case})
                        set_tests_properties(${name} PROPERTIES ENVIRONMENT "${environment}" PROCESSORS ${threads} FAIL_REGULAR_EXPRESSION "test cases: +0 [|]")
                        if(fixture_setup)
                            set_tests_properties(${name} PROPERTIES FIXTURES_SETUP ${fixture_setup})
                        endif()
                        if(fixture_needed)
                            set_tests_properties(${name} PROPERTIES FIXTURES_REQUIRED ${fixture_needed})
                        endif()
                    endforeach()
                    set(parts 1)
                    set(threads 1)
                    set(fixture_setup "")
                    set(fixture_needed "")
                endif()
            endforeach()
            add_test(NAME ${target}.${short}.count COMMAND ${target} --count --source-file=*${stem}.cpp)
            set_tests_properties(${target}.${short}.count PROPERTIES PASS_REGULAR_EXPRESSION "passing the current filters: ${case}\n")
        endforeach()
        list(JOIN all "," excluded)
        add_test(NAME ${target}.every_file_runs COMMAND ${target} --count --source-file-exclude=${excluded})
        set_tests_properties(${target}.every_file_runs PROPERTIES PASS_REGULAR_EXPRESSION "passing the current filters: 0")
    endfunction()
    chem_add_tests(chem_tests ${CHEM_TEST_SOURCES})

    # the lab layer's tests
    file(GLOB LAB_TEST_SOURCES CONFIGURE_DEPENDS tests/game/*.cpp)
    add_executable(lab_tests ${LAB_TEST_SOURCES})
    target_link_libraries(lab_tests PRIVATE chem_lab)
    target_include_directories(lab_tests SYSTEM PRIVATE extern)
    target_include_directories(lab_tests PRIVATE tests/support)
    chem_repo_root_at_run_time(lab_tests)
    chem_apply_flags(lab_tests NO_CONVERSION)
    chem_add_cases(lab_tests ${LAB_TEST_SOURCES})

    # Data that breaks a conservation law does not build: chem_datagen refuses
    # each of these (tests/data/bad_*), so the custom command above would fail.
    foreach(bad unbalanced_element unbalanced_charge unknown_species bad_formula driver_order_alone unknown_diffusion)
        add_test(NAME datagen_refuses_${bad}
            COMMAND chem_datagen ${CMAKE_SOURCE_DIR}/tests/data/bad_${bad} ${CMAKE_CURRENT_BINARY_DIR}/datagen_refused/${bad})
        set_tests_properties(datagen_refuses_${bad} PROPERTIES WILL_FAIL TRUE)
    endforeach()

    # the C API: a C11 host compiles against chem_api.h and links the shared library
    enable_language(C)
    add_executable(capi_c_smoke tests/capi/smoke.c)
    set_target_properties(capi_c_smoke PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_compile_options(capi_c_smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(capi_c_smoke PRIVATE chem_capi_shared)
    add_test(NAME capi_c_smoke COMMAND capi_c_smoke)
    add_test(NAME chem_tool_version COMMAND chem_tool version)
    add_test(NAME chem_tool_tables COMMAND chem_tool tables)
    # the demo, played through the C API with every invariant on every step,
    # reproduces its golden record digest for digest (GAME_SPEC §18: D4)
    # (The demo session against its record: lab_tests.demo_record, Stage 68, which plays it once for the gates, every
    # invariant at every step, its digests, results and notices held; `make sessions-check` plays it through chem_tool.
    # The C API's checked path in the gates is the pressure session's.)
    # the pressure session (Stage 28) against its golden record, through the C API, every invariant checked (Stage
    # 68: until then only `make sessions-check` played it)
    add_test(NAME pressure_session COMMAND chem_tool run ../../data/sessions/pressure.json --quiet --check ../../bench/sessions/pressure.jsonl)
    add_test(NAME chem_tool_study_cooling COMMAND chem_tool study cooling)
    add_test(NAME chem_tool_study_decomposition COMMAND chem_tool study decomposition)
    add_test(NAME chem_tool_study_balloon COMMAND chem_tool study balloon)
    add_test(NAME chem_tool_study_distillation COMMAND chem_tool study distillation)
    add_test(NAME chem_tool_study_dissolution COMMAND chem_tool study dissolution)
    add_test(NAME chem_tool_study_crystals COMMAND chem_tool study crystals)
    add_test(NAME chem_tool_study_solution_heat COMMAND chem_tool study solution_heat)
    add_test(NAME chem_tool_study_neutralisation COMMAND chem_tool study neutralisation)
    add_test(NAME chem_tool_study_precipitation COMMAND chem_tool study precipitation)
    add_test(NAME chem_tool_study_spectrum COMMAND chem_tool study spectrum)
    add_test(NAME chem_tool_study_tilt COMMAND chem_tool study tilt)
    add_test(NAME chem_tool_study_vortex COMMAND chem_tool study vortex)

    # The engine-free layers name no game engine.
    foreach(layer core game capi hosts/headless)
        string(REPLACE "/" "_" layer_name "${layer}")
        add_test(NAME ${layer_name}_no_engine
            COMMAND "${CMAKE_COMMAND}" -DCHEM_LAYER_DIR=${CMAKE_SOURCE_DIR}/${layer}
                    -P ${CMAKE_SOURCE_DIR}/tools/check_no_engine.cmake)
    endforeach()
    # the core computes its own transcendental functions (bit-identical replays)
    add_test(NAME core_no_libm
        COMMAND "${CMAKE_COMMAND}" -DCHEM_LAYER_DIR=${CMAKE_SOURCE_DIR}/core
                -P ${CMAKE_SOURCE_DIR}/tools/check_no_libm.cmake)
    # the Godot adapter knows the game through chem_api.h alone
    add_test(NAME godot_adapter_includes
        COMMAND "${CMAKE_COMMAND}" -DCHEM_ADAPTER_DIR=${CMAKE_SOURCE_DIR}/hosts/godot
                -P ${CMAKE_SOURCE_DIR}/tools/check_adapter_includes.cmake)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    # the Godot host's GDScript stays under budget
    add_test(NAME gdscript_budget COMMAND Python3::Interpreter -I ${CHEM_REPO_ROOT}/tools/check_gdscript_budget.py)
    # every number in data/ has a known origin, measured ones held to their source by a test (CLAUDE.md)
    add_test(NAME data_sources COMMAND Python3::Interpreter -I ${CHEM_REPO_ROOT}/tools/check_sources.py)
    add_test(NAME data_sources_self_test COMMAND Python3::Interpreter -I ${CHEM_REPO_ROOT}/tools/check_sources.py --self-test)
    # the API file godot-cpp generates its bindings from describes the installed binary
    add_test(NAME godot_api_matches_binary
        COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/check_api.py
                ${CMAKE_SOURCE_DIR}/godot-api/extension_api.json
                ${CMAKE_SOURCE_DIR}/extern/godot-cpp/gdextension/extension_api-4-7.json)
endif()
