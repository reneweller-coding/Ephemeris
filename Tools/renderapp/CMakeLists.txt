# Ephemeris Sets (16.10.2026): the family's render app (RenderKit.h, the same in every instrument; canonical in Empyrean)
# with Ephemeris's source -- its styles composed into pieces and concerts, and the player's saved sets, rendered into WAV, FLAC, Ogg Vorbis or MP3, faster than real time, each shown with
# its length.
set(_app EphemerisSets)
juce_add_gui_app(${_app}
    COMPANY_NAME     "Rene Weller"
    BUNDLE_ID        "com.reneweller.ephemeris.sets"
    PRODUCT_NAME     "${_app}"
    VERSION          ${PROJECT_VERSION}
    ICON_BIG         "${CMAKE_SOURCE_DIR}/Deploy/ephemeris.png")
target_sources(${_app} PRIVATE RenderKit.cpp RenderWindow.cpp Mp3Writer.cpp Source.cpp)
target_compile_definitions(${_app} PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:${_app},JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:${_app},JUCE_VERSION>"
    _CRT_SECURE_NO_WARNINGS)
target_link_libraries(${_app}
    PRIVATE
        EphemerisCore
        juce::juce_gui_basics
        juce::juce_data_structures
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(${_app} PRIVATE /utf-8)
endif()
if(WIN32)
    target_link_libraries(${_app} PRIVATE mfplat mfreadwrite mfuuid ole32)   # MP3 through Media Foundation
endif()
# Into bin/<preset> after every build (cmake/Family.cmake).
family_bin(${_app})
# Every format written, read back and measured (a few seconds of a Cosmic piece each), and the queue.
if(EPH_BUILD_TOOLS)
    add_test(NAME renderapp COMMAND ${_app} --selftest 6)
    set_tests_properties(renderapp PROPERTIES LABELS "audio" TIMEOUT 900)
endif()
