# ctest core_no_libm: the simulation core computes its transcendental functions
# itself (chem/math.hpp, from Stage 3), because exp/log/pow differ between C
# libraries in the last bit and the replays are bit-identical across platforms
# (GAME_SPEC §3). Nothing under native/core may call the library's (sqrt, floor,
# fabs and the classification functions are exact everywhere and allowed).
# Run as: cmake -DCHEM_LAYER_DIR=<repo>/native/core -P check_no_libm.cmake
if(NOT CHEM_LAYER_DIR)
    message(FATAL_ERROR "CHEM_LAYER_DIR not set")
endif()
file(GLOB_RECURSE files LIST_DIRECTORIES false "${CHEM_LAYER_DIR}/*.cpp" "${CHEM_LAYER_DIR}/*.hpp")
set(offenders "")
foreach(f IN LISTS files)
    file(READ "${f}" content)
    if(content MATCHES "std::(exp|expm1|log|log1p|log2|log10|pow|sin|cos|tan|cbrt|erf)[ \t]*\\(")
        list(APPEND offenders "${f}")
    endif()
endforeach()
list(LENGTH files n)
if(offenders)
    list(JOIN offenders "\n  " joined)
    message(FATAL_ERROR "${CHEM_LAYER_DIR} must not use the C library's math (chem/math.hpp instead); offending files:\n  ${joined}")
endif()
message(STATUS "no libm: ${n} files under ${CHEM_LAYER_DIR} compute their own math")
