cmake_minimum_required(VERSION 3.22)

project(AtracGlitch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ATRAC_GLITCH_BUILD_PLUGIN "Build the JUCE plug-in targets" ON)
option(ATRAC_GLITCH_BUILD_TESTS "Build the dependency-free DSP tests" ON)
set(ATRAC_GLITCH_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout")
set(EHL_JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE source checkout; avoids network when set")
set(ATRAC_GLITCH_ARTIFACT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/artifacts" CACHE PATH "Stable product staging root")

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
    if(EHL_JUCE_SOURCE_DIR)
        if(NOT EXISTS "${EHL_JUCE_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "EHL_JUCE_SOURCE_DIR does not contain JUCE: ${EHL_JUCE_SOURCE_DIR}")
        endif()
        add_subdirectory("${EHL_JUCE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce")
    elseif(ATRAC_GLITCH_JUCE_PATH)
        add_subdirectory("${ATRAC_GLITCH_JUCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
        add_subdirectory(external/JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.15
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    add_subdirectory(modules/juce-ehl-design-module)

    set(ATRAC_GLITCH_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND ATRAC_GLITCH_FORMATS AU)
    endif()

    juce_add_plugin(AtracGlitch
        COMPANY_NAME "2bit"
        BUNDLE_ID "jp.ehl.atracglitch"
        PLUGIN_MANUFACTURER_CODE Tbit
        PLUGIN_CODE Atg1
        FORMATS ${ATRAC_GLITCH_FORMATS}
        PRODUCT_NAME "ATRAC Glitch"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
    )

    target_sources(AtracGlitch PRIVATE
        Source/Plugin/PluginEditor.cpp
        Source/Plugin/PluginEditor.h
        Source/Plugin/PluginProcessor.cpp
    )

    target_include_directories(AtracGlitch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_link_libraries(AtracGlitch PRIVATE
        AtracGlitchCore
        EHL::JuceDesign
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

    set(ehl_stage_platform "linux-x64")
    if(APPLE)
        set(ehl_stage_platform "macos-arm64")
    elseif(WIN32)
        set(ehl_stage_platform "windows-x64")
    endif()
    set(atrac_glitch_juce_products "${CMAKE_CURRENT_BINARY_DIR}/AtracGlitch_artefacts/$<CONFIG>")
    set(atrac_glitch_staged_products "${ATRAC_GLITCH_ARTIFACT_ROOT}/plugin-release/${ehl_stage_platform}")
    set(atrac_glitch_stage_dependencies AtracGlitch_VST3 AtracGlitch_Standalone)
    set(atrac_glitch_stage_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory "${atrac_glitch_staged_products}/vst3"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${atrac_glitch_staged_products}/standalone"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${atrac_glitch_staged_products}/au")

    if(APPLE)
        list(APPEND atrac_glitch_stage_dependencies AtracGlitch_AU)
        list(APPEND atrac_glitch_stage_commands
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${atrac_glitch_staged_products}/vst3/atracglitch_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${atrac_glitch_staged_products}/standalone/atracglitch_standalone_plugin.app"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${atrac_glitch_staged_products}/au/atracglitch_au_plugin.component"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${atrac_glitch_juce_products}/VST3/ATRAC Glitch.vst3" "${atrac_glitch_staged_products}/vst3/atracglitch_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${atrac_glitch_juce_products}/Standalone/ATRAC Glitch.app" "${atrac_glitch_staged_products}/standalone/atracglitch_standalone_plugin.app"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${atrac_glitch_juce_products}/AU/ATRAC Glitch.component" "${atrac_glitch_staged_products}/au/atracglitch_au_plugin.component"
            COMMAND codesign --force --deep --sign - "${atrac_glitch_staged_products}/vst3/atracglitch_vst3_plugin.vst3"
            COMMAND codesign --force --deep --sign - "${atrac_glitch_staged_products}/standalone/atracglitch_standalone_plugin.app"
            COMMAND codesign --force --deep --sign - "${atrac_glitch_staged_products}/au/atracglitch_au_plugin.component")
    elseif(WIN32)
        list(APPEND atrac_glitch_stage_commands
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${atrac_glitch_staged_products}/vst3/atracglitch_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${atrac_glitch_juce_products}/VST3/ATRAC Glitch.vst3" "${atrac_glitch_staged_products}/vst3/atracglitch_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy "${atrac_glitch_juce_products}/Standalone/ATRAC Glitch.exe" "${atrac_glitch_staged_products}/standalone/atracglitch_standalone_plugin.exe")
    else()
        list(APPEND atrac_glitch_stage_commands
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${atrac_glitch_juce_products}/VST3/ATRAC Glitch.vst3" "${atrac_glitch_staged_products}/vst3/atracglitch_vst3_plugin.vst3"
            COMMAND ${CMAKE_COMMAND} -E copy "${atrac_glitch_juce_products}/Standalone/ATRAC Glitch" "${atrac_glitch_staged_products}/standalone/atracglitch_standalone_plugin")
    endif()

    list(APPEND atrac_glitch_stage_commands
        COMMAND ${CMAKE_COMMAND}
            -DPRODUCT_NAME=AtracGlitch
            -DSLUG=atracglitch
            -DBUNDLE_ID=jp.ehl.atracglitch
            -DPLUGIN_CODE=Atg1
            -DSTAGE_DIR=${atrac_glitch_staged_products}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteArtifactsManifest.cmake")

    add_custom_target(ehl_stage_products
        ${atrac_glitch_stage_commands}
        DEPENDS ${atrac_glitch_stage_dependencies}
        COMMENT "Staging AtracGlitch products under ${ATRAC_GLITCH_ARTIFACT_ROOT}/plugin-release")

    if(ATRAC_GLITCH_BUILD_TESTS)
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

        add_executable(AtracGlitchEditorTests Tests/EditorTests.cpp)
        target_include_directories(AtracGlitchEditorTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
        target_link_libraries(AtracGlitchEditorTests PRIVATE
            AtracGlitch
            EHL::JuceDesign
            juce::juce_audio_utils
        )
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
        add_dependencies(AtracGlitchCoreTests AtracGlitchFFmpegInteropTests)
        add_test(NAME AtracGlitch.Interop.FFmpeg COMMAND AtracGlitchFFmpegInteropTests)
    endif()

    if(ATRAC_GLITCH_BUILD_PLUGIN)
        add_test(NAME AtracGlitch.Editor COMMAND AtracGlitchEditorTests)
    endif()

    if(APPLE AND ATRAC_GLITCH_BUILD_PLUGIN)
        add_test(NAME AtracGlitch.Host.VST3
            COMMAND AtracGlitchPluginLoadTests
                "$<TARGET_BUNDLE_DIR:AtracGlitch_VST3>"
                "ATRAC Glitch"
        )
        add_test(
            NAME AtracGlitch.ArtifactChecks
            COMMAND ${CMAKE_COMMAND}
                -DSTAGE_DIR=${atrac_glitch_staged_products}
                -DSLUG=atracglitch
                -DEXPECT_AU=$<BOOL:${APPLE}>
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Tests/VerifyArtifacts.cmake")
    endif()
endif()
