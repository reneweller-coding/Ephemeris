# On Android the executables are cross-compiled for the headset; ctest must not run them here.
function(eph_add_test name)
    if(NOT ANDROID)
        add_test(NAME ${name} COMMAND ${ARGN})
    endif()
endfunction()

add_executable(eph_selftest selftest.cpp)
target_link_libraries(eph_selftest PRIVATE EphemerisCore EphemerisPieces)
# One ctest test per self-test section, `selftest.<name>`, read from the registration table in selftest.cpp
# ({ "testName", testName }) when CMake configures, so a new section needs no edit here (after Phosphene,
# which asks the executable instead). CONFIGURE_DEPENDS-like: selftest.cpp is a configure dependency.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/selftest.cpp)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/selftest.cpp EPH_SECTION_LINES REGEX "^    \{ \"test[A-Za-z0-9]+\", test[A-Za-z0-9]+ \},")
foreach(line ${EPH_SECTION_LINES})
    string(REGEX MATCH "\"(test[A-Za-z0-9]+)\"" _ "${line}")
    eph_add_test(selftest.${CMAKE_MATCH_1} eph_selftest --only ${CMAKE_MATCH_1})
endforeach()

# The lane paths against the scalar reference, bit for bit, once per vector path. The main variant is
# built as the core is (AVX2 on x86-64, NEON on the Quest); on x86 the NEON path also runs through
# neonshim/arm_neon.h, and the scalar path is forced. The variants compile the half-band design
# themselves: linking EphemerisCore would bring its AVX2 flags.
add_executable(eph_vectest vectest.cpp)
target_link_libraries(eph_vectest PRIVATE EphemerisCore)
eph_add_test(vectest eph_vectest)
set(EPH_TEST_TARGETS eph_selftest eph_vectest)

if(NOT ANDROID AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|x64)")
    if(EPH_AVX2)
        target_compile_definitions(eph_vectest PRIVATE EPH_EXPECT_PATH="avx2")
    endif()
    foreach(variant neon scalar)
        add_executable(eph_vectest_${variant} vectest.cpp ${CMAKE_SOURCE_DIR}/Core/src/Halfband.cpp)
        target_include_directories(eph_vectest_${variant} PRIVATE ${CMAKE_SOURCE_DIR}/Core/include)
        target_compile_features(eph_vectest_${variant} PRIVATE cxx_std_20)
        eph_add_test(vectest_${variant} eph_vectest_${variant})
        list(APPEND EPH_TEST_TARGETS eph_vectest_${variant})
    endforeach()
    target_include_directories(eph_vectest_neon BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/neonshim)
    target_compile_definitions(eph_vectest_neon PRIVATE EPH_NEON_SHIM=1 EPH_EXPECT_PATH="neon-shim")
    target_compile_definitions(eph_vectest_scalar PRIVATE EPH_FORCE_SCALAR=1 EPH_EXPECT_PATH="scalar")
elseif(ANDROID AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    target_compile_definitions(eph_vectest PRIVATE EPH_EXPECT_PATH="neon")
endif()

foreach(t ${EPH_TEST_TARGETS})
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
endforeach()
