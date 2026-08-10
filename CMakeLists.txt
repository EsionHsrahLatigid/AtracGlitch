cmake_minimum_required(VERSION 3.22)

project(AtracGlitch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ATRAC_GLITCH_BUILD_PLUGIN "Build the JUCE plug-in targets" ON)
option(ATRAC_GLITCH_BUILD_TESTS "Build the dependency-free DSP tests" ON)
set(ATRAC_GLITCH_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout")

add_library(AtracGlitchCore STATIC
    Source/DSP/AtracCodec.cpp
    Source/DSP/AtracGlitchEngine.cpp
)

set_target_properties(AtracGlitchCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(AtracGlitchCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_features(AtracGlitchCore PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(AtracGlitchCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(AtracGlitchCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ATRAC_GLITCH_BUILD_PLUGIN)
    if(ATRAC_GLITCH_JUCE_PATH)
        add_subdirectory("${ATRAC_GLITCH_JUCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
        add_subdirectory(external/JUCE)
    else()
        find_package(JUCE CONFIG REQUIRED)
    endif()

    set(ATRAC_GLITCH_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND ATRAC_GLITCH_FORMATS AU)
    endif()

    juce_add_plugin(AtracGlitch
        COMPANY_NAME "2bit"
        BUNDLE_ID "com.2bit.atracglitch"
        PLUGIN_MANUFACTURER_CODE Tbit
        PLUGIN_CODE Atg1
        FORMATS ${ATRAC_GLITCH_FORMATS}
        PRODUCT_NAME "ATRAC Glitch"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
    )

    target_sources(AtracGlitch PRIVATE
        Source/Plugin/PluginProcessor.cpp
    )

    target_include_directories(AtracGlitch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_link_libraries(AtracGlitch PRIVATE
        AtracGlitchCore
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
    )

    target_compile_definitions(AtracGlitch PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )

    if(APPLE)
        foreach(format_target AtracGlitch_VST3 AtracGlitch_AU AtracGlitch_Standalone)
            add_custom_command(TARGET ${format_target} POST_BUILD
                COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${format_target}>"
                COMMENT "Applying an ad-hoc signature to ${format_target}"
                VERBATIM
            )
        endforeach()
    endif()

    if(APPLE AND ATRAC_GLITCH_BUILD_TESTS)
        juce_add_console_app(AtracGlitchPluginLoadTests
            PRODUCT_NAME "ATRAC Glitch Plugin Load Tests"
        )
        target_sources(AtracGlitchPluginLoadTests PRIVATE Tests/PluginLoadTests.cpp)
        target_include_directories(AtracGlitchPluginLoadTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
        target_link_libraries(AtracGlitchPluginLoadTests PRIVATE
            juce::juce_audio_processors_headless
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
        )
        target_compile_definitions(AtracGlitchPluginLoadTests PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_USE_CURL=0
        )
        add_dependencies(AtracGlitchPluginLoadTests AtracGlitch_VST3)
    endif()
endif()

if(ATRAC_GLITCH_BUILD_TESTS)
    enable_testing()
    add_executable(AtracGlitchCoreTests Tests/CoreTests.cpp)
    target_link_libraries(AtracGlitchCoreTests PRIVATE AtracGlitchCore)
    target_compile_features(AtracGlitchCoreTests PRIVATE cxx_std_20)
    add_test(NAME AtracGlitch.Core COMMAND AtracGlitchCoreTests)

    add_executable(AtracGlitchRealtimeTests Tests/RealtimeTests.cpp)
    target_link_libraries(AtracGlitchRealtimeTests PRIVATE AtracGlitchCore)
    target_compile_features(AtracGlitchRealtimeTests PRIVATE cxx_std_20)
    add_test(NAME AtracGlitch.Realtime COMMAND AtracGlitchRealtimeTests)

    find_program(ATRAC_GLITCH_FFMPEG_EXECUTABLE ffmpeg)
    if(ATRAC_GLITCH_FFMPEG_EXECUTABLE)
        add_executable(AtracGlitchFFmpegInteropTests Tests/FFmpegInteropTests.cpp)
        target_link_libraries(AtracGlitchFFmpegInteropTests PRIVATE AtracGlitchCore)
        target_compile_features(AtracGlitchFFmpegInteropTests PRIVATE cxx_std_20)
        target_compile_definitions(AtracGlitchFFmpegInteropTests PRIVATE
            FFMPEG_EXECUTABLE="${ATRAC_GLITCH_FFMPEG_EXECUTABLE}"
        )
        add_test(NAME AtracGlitch.Interop.FFmpeg COMMAND AtracGlitchFFmpegInteropTests)
    endif()

    if(APPLE AND ATRAC_GLITCH_BUILD_PLUGIN)
        add_test(NAME AtracGlitch.Host.VST3
            COMMAND AtracGlitchPluginLoadTests
                "$<TARGET_BUNDLE_DIR:AtracGlitch_VST3>"
                "ATRAC Glitch"
        )
    endif()
endif()
