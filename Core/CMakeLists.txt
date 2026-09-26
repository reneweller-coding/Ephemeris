add_library(EphemerisCore STATIC
    src/Params.cpp
    src/Clock.cpp
    src/Score.cpp
    src/Midi.cpp
    src/WavWriter.cpp
    src/Halfband.cpp
    src/compose/Harmony.cpp
    src/compose/GestureEngine.cpp
    src/Rack.cpp
    src/synth/ModVoice.cpp
    src/fx/TapeEcho.cpp
    src/fx/Reverb.cpp
    src/synth/TapeKeys.cpp
    src/compose/Pads.cpp
    src/synth/Atmos.cpp
    src/fx/Dynamics.cpp
    src/synth/StringMachine.cpp
    src/fx/Spring.cpp
    src/compose/Style.cpp
    src/compose/Form.cpp
    src/compose/Composer.cpp
    src/SetFile.cpp
    src/synth/Drums.cpp
    src/compose/Lead.cpp
    src/Engine.cpp
    src/Cue.cpp
    src/Loudness.cpp
    src/Leveler.cpp
    src/Presets.cpp
    src/fx/Plate.cpp
    src/fx/Bbd.cpp
    src/synth/Wavetable.cpp
    src/synth/WavetableData.cpp
    src/synth/Poly.cpp
)
target_include_directories(EphemerisCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(EphemerisCore PUBLIC ws2_32)   # the cue sender's UDP socket (Cue.h)
endif()
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
