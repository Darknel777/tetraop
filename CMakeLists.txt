cmake_minimum_required(VERSION 3.22.1)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "Runtime Library")
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "Build for 10.15")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_STANDALONE "Build Standalone plugin format" ON) # Allow overriding from the command line

project(TetraOP VERSION 1.0.3)

add_compile_definitions(PROJECT_VERSION="${PROJECT_VERSION}")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_XCODE_GENERATE_SCHEME OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(libs/JUCE)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)
add_subdirectory(libs/surge-tuning)
add_subdirectory(libs/gin/modules)

add_library(mts-esp-client STATIC libs/MTS-ESP/Client/libMTSClient.cpp)
target_include_directories(mts-esp-client PUBLIC libs/MTS-ESP/Client)
if (UNIX AND NOT APPLE)
     target_link_libraries(mts-esp-client PUBLIC dl)
endif ()

set(plugin_formats)

if(WIN32)
    list(APPEND plugin_formats VST3)
elseif(APPLE)
    list(APPEND plugin_formats VST3 AU)
elseif(UNIX)
    list(APPEND plugin_formats VST3 LV2)
endif()

if(BUILD_STANDALONE)
    list(APPEND plugin_formats Standalone)
endif()

juce_add_plugin(${PROJECT_NAME}
        COMPANY_NAME Tilr
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        JUCE_VST3_CAN_REPLACE_VST2 FALSE
        COPY_PLUGIN_AFTER_BUILD TRUE
        PLUGIN_MANUFACTURER_CODE Tilr
        PLUGIN_CODE t4op
        FORMATS ${plugin_formats}
        PRODUCT_NAME "TetraOP"
        BUNDLE_ID "com.tilr.tetraop"
        LV2URI "https://github.com/tiagolr/tetraop"
)

clap_juce_extensions_plugin(TARGET ${PROJECT_NAME}
        CLAP_ID "com.tilr.tetraop"
        CLAP_FEATURES instrument synthesizer)

#target_include_directories(${PROJECT_NAME}
#    PRIVATE
#        ${CMAKE_CURRENT_SOURCE_DIR}/libs/xsimd/include
#)

file(GLOB_RECURSE src
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB MAIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
source_group(Source FILES ${MAIN_SOURCES})

file(GLOB UI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp
)
source_group(Source\\ui FILES ${UI_SOURCES})

file(GLOB UI_PANEL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/panels/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/panels/*.cpp
)
source_group(Source\\ui\\panels FILES ${UI_PANEL_SOURCES})

file(GLOB UI_WIDGET_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/widgets/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/widgets/*.cpp
)
source_group(Source\\ui\\widgets FILES ${UI_WIDGET_SOURCES})

file(GLOB FX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/fx/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/fx/*.cpp
)
source_group(Source\\dsp\\fx FILES ${FX_SOURCES})

file(GLOB UI_FX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/fx/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/fx/*.cpp
)
source_group(Source\\ui\\fx FILES ${UI_FX_SOURCES})
#
file(GLOB DSP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.cpp
)
source_group(Source\\dsp FILES ${DSP_SOURCES})

file(GLOB FILTER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/filter/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/filter/*.cpp
)
source_group(Source\\dsp\\filter FILES ${FILTER_SOURCES})

file(GLOB ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.cpp
)
source_group(Source\\engine FILES ${ENGINE_SOURCES})

# Make the SourceFiles buildable
target_sources(${PROJECT_NAME} PRIVATE ${src})

# These are some toggleable options from the JUCE CMake API
target_compile_definitions(${PROJECT_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# JUCE libraries to bring into our project
juce_generate_juce_header(${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        juce::juce_core
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_audio_utils
        juce::juce_dsp
        mts-esp-client
        tuning-library
        gin
        gin_dsp
        gin_svg
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(${PROJECT_NAME} PRIVATE
    libs/mverb
)

file(GLOB_RECURSE res "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.*")
juce_add_binary_data(${PROJECT_NAME}_res SOURCES ${res})
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_res)

if(APPLE)
    target_compile_definitions(${PROJECT_NAME} PUBLIC JUCE_AU=1)
endif()
