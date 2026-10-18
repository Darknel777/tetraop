cmake_minimum_required(VERSION 3.22)

# Xen Milenium — отдельный продукт. Этот файл не собирает TetraOP и не
# подключает исходники XEN 2000. JUCE берётся FetchContent-ом из официального
# репозитория, а не из libs/JUCE этого чекаута.
project(XenMilenium VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# Идентификаторы выбраны по docs/CMake API.md JUCE 9.0.4:
# PLUGIN_MANUFACTURER_CODE — ровно 4 символа, для AU хотя бы одна заглавная;
# GarageBand 10.3: первая заглавная, остальные строчные.
# PLUGIN_CODE — ровно 4 символа и ровно одна заглавная; GarageBand 10.3:
# первая заглавная, остальные строчные.
# Это не Xen0 / Xn20 и не com.XEN.XEN2000.
juce_add_plugin(XenMilenium
    COMPANY_NAME "XenMilenium"
    BUNDLE_ID "com.xenmilenium.xenmilenium"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Xmnl
    PLUGIN_CODE Xmil
    FORMATS VST3
    PRODUCT_NAME "Xen Milenium"
    PLUGIN_NAME "Xen Milenium"
    VST3_CATEGORIES "Instrument Synth"
    DESCRIPTION "Xen Milenium stage 0 scaffold")

juce_generate_juce_header(XenMilenium)

target_sources(XenMilenium
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp)

target_include_directories(XenMilenium PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(XenMilenium
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_features(XenMilenium PUBLIC cxx_std_17)

target_link_libraries(XenMilenium
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

juce_add_console_app(XenMilenium_Smoke
    PRODUCT_NAME "XenMilenium Smoke")

juce_generate_juce_header(XenMilenium_Smoke)

target_sources(XenMilenium_Smoke
    PRIVATE
        Tests/SilenceAndMidiSmoke.cpp
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp)

target_include_directories(XenMilenium_Smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(XenMilenium_Smoke
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_features(XenMilenium_Smoke PRIVATE cxx_std_17)

target_link_libraries(XenMilenium_Smoke
    PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME SilenceAndMidiSmoke COMMAND $<TARGET_FILE:XenMilenium_Smoke>)
