add_executable(eph_render main.cpp)
target_link_libraries(eph_render PRIVATE EphemerisCore EphemerisPieces)
if(MSVC)
    target_compile_options(eph_render PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(eph_render PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Into bin/<preset> after every build (cmake/Family.cmake, 01.10.2026).
family_bin(eph_render)
