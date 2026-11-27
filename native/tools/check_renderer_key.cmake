# ctest renderer_key_moves (Stage 86): the renderer's key (native/cmake/renderer_key.cmake) is a function of its
# files' names and contents alone: the same files give the same key wherever they are read from; a byte changed in one,
# or one named otherwise, gives another; and it refuses lists that do not pair up.
#
#   cmake -DSCRIPT=<renderer_key.cmake> -DWORK=<a scratch directory> -P check_renderer_key.cmake
set(a "${WORK}/a")
set(b "${WORK}/b")
file(MAKE_DIRECTORY "${a}" "${b}")

function(key_of out names files)
	# (a quoted argument keeps its semicolons: each list whole)
	execute_process(COMMAND "${CMAKE_COMMAND}" "-DOUT=${WORK}/key.hpp" "-DNAMES=${names}" "-DFILES=${files}" -P "${SCRIPT}"
		RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
	if(NOT status EQUAL 0)
		set(${out} "refused" PARENT_SCOPE)
		return()
	endif()
	file(READ "${WORK}/key.hpp" header)
	string(REGEX MATCH "RENDERER_KEY = \"([0-9a-f]+)\"" _ "${header}")
	set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

function(expect what got want)
	if(NOT got STREQUAL want)
		message(FATAL_ERROR "renderer_key: ${what}: ${got}, expected ${want}")
	endif()
endfunction()

file(WRITE "${a}/pixel.glsl" "float shade() { return 0.5; }\n")
file(WRITE "${a}/media.json" "{\"water\": 1.333}\n")
file(WRITE "${b}/pixel.glsl" "float shade() { return 0.5; }\n")
file(WRITE "${b}/media.json" "{\"water\": 1.333}\n")
set(names "shader/pixel.glsl;data/media.json")

key_of(first "${names}" "${a}/pixel.glsl;${a}/media.json")
string(LENGTH "${first}" length)
expect("its length" "${length}" "16")
key_of(again "${names}" "${a}/pixel.glsl;${a}/media.json")
expect("the same files again" "${again}" "${first}")
key_of(elsewhere "${names}" "${b}/pixel.glsl;${b}/media.json")
expect("the same files read from elsewhere" "${elsewhere}" "${first}")

file(WRITE "${b}/pixel.glsl" "float shade() { return 0.6; }\n")
key_of(changed "${names}" "${b}/pixel.glsl;${b}/media.json")
if(changed STREQUAL first OR changed STREQUAL "refused")
	message(FATAL_ERROR "renderer_key: a byte of the shader changed and the key did not (${changed})")
endif()

key_of(renamed "shader/other.glsl;data/media.json" "${a}/pixel.glsl;${a}/media.json")
if(renamed STREQUAL first OR renamed STREQUAL "refused")
	message(FATAL_ERROR "renderer_key: a file named otherwise and the key did not change (${renamed})")
endif()

key_of(unpaired "${names}" "${a}/pixel.glsl")
expect("names and files that do not pair up" "${unpaired}" "refused")
key_of(missing "${names}" "${a}/pixel.glsl;${a}/absent.json")
expect("a file that is not there" "${missing}" "refused")
message(STATUS "renderer_key: a function of its files' names and contents")
