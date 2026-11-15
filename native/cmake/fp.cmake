# Compiler flags shared by every chem target.
#
# Floating point: the simulation's replays are bit-identical (GAME_SPEC §3,
# determinism). A fused multiply-add rounds a*b+c once, and fast-math licenses
# reassociation; both change results in the last bit, so both are forbidden on
# every chem target (and checked by chem_tests: fma_sensitive).
#
# Warnings are errors. -Wconversion is on by default; pass NO_CONVERSION for
# targets that expand godot-cpp binding macros or doctest's.
function(chem_apply_flags target)
    cmake_parse_arguments(CHEM "NO_CONVERSION" "" "" ${ARGN})
    if(MSVC)
        target_compile_options(${target} PRIVATE /fp:precise /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -ffp-contract=off -fno-fast-math
            $<$<CONFIG:RelWithDebInfo>:-O3>
            -Wall -Wextra -Wpedantic -Werror -Wshadow)
        if(NOT CHEM_NO_CONVERSION)
            target_compile_options(${target} PRIVATE -Wconversion)
        endif()
    endif()
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
endfunction()
