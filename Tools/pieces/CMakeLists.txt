# The fixed pieces of Phases 1 to 3 (Study.h, Sketch.h): no longer part of the instrument -- the
# composer writes the pieces now -- but kept as reference pieces for the tests (the block-size test
# renders the study) and for eph_render --study / --sketch.
add_library(EphemerisPieces STATIC src/Study.cpp src/Sketch.cpp)
target_include_directories(EphemerisPieces PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(EphemerisPieces PUBLIC EphemerisCore)
if(MSVC)
    target_compile_options(EphemerisPieces PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(EphemerisPieces PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
