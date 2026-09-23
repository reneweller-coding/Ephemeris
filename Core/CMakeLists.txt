add_library(EphemerisCore STATIC
    src/Params.cpp
    src/Clock.cpp
    src/Score.cpp
    src/Midi.cpp
    src/WavWriter.cpp
    src/Halfband.cpp
    src/Harmony.cpp
    src/GestureEngine.cpp
    src/Rack.cpp
    src/ModVoice.cpp
    src/TapeEcho.cpp
    src/Reverb.cpp
    src/TapeKeys.cpp
    src/Pads.cpp
    src/Atmos.cpp
    src/Dynamics.cpp
    src/StringMachine.cpp
    src/Spring.cpp
    src/Style.cpp
    src/Form.cpp
    src/Composer.cpp
    src/SetFile.cpp
    src/Study.cpp
    src/Lead.cpp
    src/Sketch.cpp
    src/Engine.cpp
)
target_include_directories(EphemerisCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(EphemerisCore PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(EphemerisCore PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(EphemerisCore PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(EphemerisCore PRIVATE -Wall -Wextra)
endif()

# AVX2 with FMA for the lane-parallel paths (Vec.h). PUBLIC, so everything that links the core sees
# the same vector path the core was built with. The arm64 build (Quest) takes NEON on its own.
if(EPH_AVX2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|x64)")
    if(MSVC)
        target_compile_options(EphemerisCore PUBLIC /arch:AVX2)
    else()
        target_compile_options(EphemerisCore PUBLIC -mavx2 -mfma)
    endif()
endif()
