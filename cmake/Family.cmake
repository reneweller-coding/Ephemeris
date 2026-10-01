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
# The same file runs as a script (cmake -P) for the copy itself.

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
