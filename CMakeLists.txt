cmake_minimum_required(VERSION 3.22)

# macOS Catalina (10.15) es la última que soporta la MacBook Pro mid 2012: compilamos para 10.13+
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Versión mínima de macOS")

project(ControlTower VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.10
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(ControlTower
    PRODUCT_NAME "Control Tower"
    COMPANY_NAME "PaloGol"
    BUNDLE_ID "com.palogol.controltower"
    VERSION ${PROJECT_VERSION}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Control Tower usa la entrada de audio para el micrófono o la línea en vivo.")

target_sources(ControlTower PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Engine.cpp
    Source/Controller.cpp)

target_include_directories(ControlTower PRIVATE Source)

target_compile_definitions(ControlTower PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ControlTower,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ControlTower,JUCE_VERSION>"
    # en Mac, mp3/m4a/aac los decodifica Core Audio; en otros sistemas, el decodificador mp3 de JUCE
    $<$<NOT:$<PLATFORM_ID:Darwin>>:JUCE_USE_MP3AUDIOFORMAT=1>)

target_link_libraries(ControlTower
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Pruebas (sólo desarrollo / CI):  cmake -B build -DCT_TESTS=ON
option(CT_TESTS "Compilar pruebas del motor" OFF)
if(CT_TESTS)
    juce_add_console_app(EngineTest PRODUCT_NAME "EngineTest")
    target_sources(EngineTest PRIVATE tests/engine_test.cpp Source/Engine.cpp Source/Controller.cpp)
    target_include_directories(EngineTest PRIVATE Source)
    target_compile_definitions(EngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        $<$<NOT:$<PLATFORM_ID:Darwin>>:JUCE_USE_MP3AUDIOFORMAT=1>)
    target_link_libraries(EngineTest PRIVATE juce::juce_audio_devices juce::juce_audio_formats juce::juce_gui_basics
                                     PUBLIC juce::juce_recommended_config_flags)
endif()
