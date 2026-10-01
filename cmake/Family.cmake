# The layout every instrument of the family shares (01.10.2026); the same file in each repository.
#
#   build/<preset>/   the build trees, one per preset of CMakePresets.json (msvc, icx, release; quest by its script)
#   bin/<preset>/     what can be started, flat: the standalone, the VST3, the renderer and the files they read
#   dist/             what is handed out: setup, portable zip, Quest APK, checksums (Deploy/build_release.ps1)
#   work/             local data, renders and logs; never in git
#
# family_bin(<target> [VST3] [FILES <file>...]) copies a target's product into FAMILY_BIN_DIR after every build of it --
# a VST3 as its whole bundle --, and the FILES beside it. With a Visual Studio tree, Release goes to bin/<preset> and any
# other configuration beside it (bin/<preset>-Debug). A copy that fails -- the program is running -- warns and does not
# fail the build. FAMILY_BIN_DIR is set by the presets; empty (a release tree) copies nothing.
#
# family_doccheck() adds the test `doccheck` where Doxygen is installed (01.10.2026): every class, function, variable,
# typedef and macro of the sources -- Core, Plugin, the Quest app, the tools, the tests; private members, static functions
# and anonymous namespaces included -- has its documentation, or the test fails and names what lacks it.
#
# The same file runs as a script (cmake -P) for the copy itself and for the documentation check.

if(CMAKE_SCRIPT_MODE_FILE AND DOCCHECK)
    # cmake -DDOCCHECK=1 -DDOXYGEN=<doxygen> -DROOT=<source dir> -DOUT=<work dir> -DNAME=<project> -P Family.cmake
    set(inputs "")
    foreach(d Core/include Core/src Plugin Quest/src Tools/render Tools/plandump Tests)
        if(IS_DIRECTORY "${ROOT}/${d}")
            string(APPEND inputs " \"${ROOT}/${d}\"")
        endif()
    endforeach()
    file(MAKE_DIRECTORY "${OUT}")
    set(log "${OUT}/undocumented.txt")
    file(REMOVE "${log}")
    file(WRITE "${OUT}/Doxyfile" "PROJECT_NAME = \"${NAME}\"
OUTPUT_DIRECTORY = \"${OUT}\"
INPUT = ${inputs}
FILE_PATTERNS = *.h *.cpp
RECURSIVE = YES
EXCLUDE = \"${ROOT}/Tests/neonshim\" \"${ROOT}/Tests/neonbench\"
EXCLUDE_PATTERNS = */ThirdParty/* */build/* */JuceLibraryCode/*
EXTRACT_ALL = NO
EXTRACT_PRIVATE = YES
EXTRACT_STATIC = YES
EXTRACT_LOCAL_CLASSES = YES
EXTRACT_ANON_NSPACES = YES
EXTRACT_LOCAL_METHODS = YES
DISTRIBUTE_GROUP_DOC = YES
WARN_IF_UNDOCUMENTED = YES
WARN_IF_DOC_ERROR = NO
WARN_NO_PARAMDOC = NO
WARN_IF_INCOMPLETE_DOC = NO
WARN_LOGFILE = \"${log}\"
GENERATE_HTML = NO
GENERATE_LATEX = NO
QUIET = YES
HAVE_DOT = NO
")
    execute_process(COMMAND "${DOXYGEN}" "${OUT}/Doxyfile" WORKING_DIRECTORY "${ROOT}" RESULT_VARIABLE r OUTPUT_QUIET ERROR_QUIET)
    if(NOT r EQUAL 0)
        message(FATAL_ERROR "doxygen failed (${r})")
    endif()
    set(missing "")
    if(EXISTS "${log}")
        file(STRINGS "${log}" lines REGEX "is not documented")
        set(missing ${lines})
    endif()
    list(LENGTH missing n)
    if(n GREATER 0)
        list(SUBLIST missing 0 40 shown)
        string(REPLACE ";" "\n" shown "${shown}")
        message(FATAL_ERROR "${n} undocumented (all of them in ${log}):\n${shown}")
    endif()
    message(STATUS "doccheck: everything documented")
    return()
endif()

if(CMAKE_SCRIPT_MODE_FILE)
    # cmake -DFROM=<file or folder> -DTO=<file or folder> -P Family.cmake
    if(IS_DIRECTORY "${FROM}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${FROM}" "${TO}" RESULT_VARIABLE r)
    else()
        get_filename_component(dir "${TO}" DIRECTORY)
        file(MAKE_DIRECTORY "${dir}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FROM}" "${TO}" RESULT_VARIABLE r)
    endif()
    if(NOT r EQUAL 0)
        message(WARNING "bin: could not copy ${FROM} to ${TO} (is it running?) -- the build tree has the new one")
    endif()
    return()
endif()

set(FAMILY_BIN_DIR "" CACHE PATH "Where the startable programs are copied after each build (bin/<preset>); empty: nowhere")
set(FAMILY_LAYOUT_FILE "${CMAKE_CURRENT_LIST_FILE}" CACHE INTERNAL "")

function(family_bin target)
    if(NOT FAMILY_BIN_DIR OR NOT TARGET ${target})
        return()
    endif()
    cmake_parse_arguments(A "VST3" "" "FILES" ${ARGN})
    get_property(multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(multi)
        set(dir "${FAMILY_BIN_DIR}$<$<NOT:$<CONFIG:Release>>:-$<CONFIG>>")
    else()
        set(dir "${FAMILY_BIN_DIR}")
    endif()
    if(A_VST3)
        # JUCE's VST3 is a folder (<name>.vst3/Contents/x86_64-win/<name>.vst3): the bundle is two levels up.
        set(from "$<TARGET_FILE_DIR:${target}>/../..")
    else()
        set(from "$<TARGET_FILE:${target}>")
    endif()
    set(cmds COMMAND "${CMAKE_COMMAND}" "-DFROM=${from}" "-DTO=${dir}/$<TARGET_FILE_NAME:${target}>" -P "${FAMILY_LAYOUT_FILE}")
    foreach(f IN LISTS A_FILES)
        get_filename_component(name "${f}" NAME)
        list(APPEND cmds COMMAND "${CMAKE_COMMAND}" "-DFROM=${f}" "-DTO=${dir}/${name}" -P "${FAMILY_LAYOUT_FILE}")
    endforeach()
    add_custom_command(TARGET ${target} POST_BUILD ${cmds} VERBATIM)
endfunction()

# family_doccheck(): the test `doccheck` (see the top of this file), where Doxygen is found; call it where enable_testing()
# is in effect.
function(family_doccheck)
    find_program(FAMILY_DOXYGEN doxygen PATHS "C:/Program Files/doxygen/bin")
    if(NOT FAMILY_DOXYGEN)
        message(STATUS "doccheck: no doxygen, no test")
        return()
    endif()
    add_test(NAME doccheck COMMAND "${CMAKE_COMMAND}" -DDOCCHECK=1 "-DDOXYGEN=${FAMILY_DOXYGEN}" "-DROOT=${CMAKE_SOURCE_DIR}"
                                   "-DOUT=${CMAKE_BINARY_DIR}/doccheck" "-DNAME=${PROJECT_NAME}" -P "${FAMILY_LAYOUT_FILE}")
    set_tests_properties(doccheck PROPERTIES LABELS "docs")
endfunction()

# family_juce(<tag>): JUCE for the plugin -- this repository's ThirdParty/JUCE, else the sibling Phosphene's checkout,
# else fetched from GitHub at <tag>. Nothing under Core/ ever includes JUCE.
macro(family_juce tag)
    include(FetchContent)
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    foreach(_family_juce_dir "${CMAKE_SOURCE_DIR}/ThirdParty/JUCE" "${CMAKE_SOURCE_DIR}/../PsytranceGenerator/ThirdParty/JUCE")
        if(NOT FETCHCONTENT_SOURCE_DIR_JUCE AND EXISTS "${_family_juce_dir}/CMakeLists.txt")
            set(FETCHCONTENT_SOURCE_DIR_JUCE "${_family_juce_dir}" CACHE PATH "" FORCE)
        endif()
    endforeach()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${tag}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endmacro()
