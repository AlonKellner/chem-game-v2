# ctest *_no_engine: nothing under CHEM_LAYER_DIR names a game engine -- Godot
# (godot-cpp, the GDExtension interface), Unity, Unreal. The engine-free layers:
# native/core, native/game, native/capi, native/hosts/headless.
# Run as: cmake -DCHEM_LAYER_DIR=<dir> -P check_no_engine.cmake
if(NOT CHEM_LAYER_DIR)
    message(FATAL_ERROR "CHEM_LAYER_DIR not set")
endif()
file(GLOB_RECURSE files LIST_DIRECTORIES false "${CHEM_LAYER_DIR}/*")
set(offenders "")
foreach(f IN LISTS files)
    file(READ "${f}" content)
    if(content MATCHES "godot_cpp|<godot|\"godot|gdextension_interface|UnityEngine|IUnityInterface|CoreMinimal\\.h|UObject")
        list(APPEND offenders "${f}")
    endif()
endforeach()
list(LENGTH files n)
if(offenders)
    list(JOIN offenders "\n  " joined)
    message(FATAL_ERROR "${CHEM_LAYER_DIR} must not depend on a game engine; offending files:\n  ${joined}")
endif()
message(STATUS "no engine: ${n} files under ${CHEM_LAYER_DIR} are engine-free")
