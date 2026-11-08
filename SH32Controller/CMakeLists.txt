cmake_minimum_required(VERSION 3.22)
project(SH32Controller VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")

# --- JUCE : soit un dossier local (-DSH32_JUCE_DIR=...), soit téléchargé depuis GitHub
set(SH32_JUCE_DIR "" CACHE PATH "Chemin d'un checkout JUCE existant (optionnel)")
set(SH32_JUCE_TAG "9.0.5" CACHE STRING "Tag JUCE à télécharger si SH32_JUCE_DIR est vide")
option(SH32_COPY_AFTER_BUILD "Copier le .vst3 dans le dossier VST3 système après compilation" OFF)
option(SH32_BUILD_TESTS "Compiler les tests du moteur (sans JUCE)" ON)

if(SH32_JUCE_DIR)
    add_subdirectory(${SH32_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${SH32_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

juce_add_plugin(SH32Controller
    COMPANY_NAME "SH32Tools"
    PLUGIN_MANUFACTURER_CODE Sh32
    PLUGIN_CODE Shct
    FORMATS VST3
    PRODUCT_NAME "SH-32 Controller"
    VST3_CATEGORIES Instrument
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${SH32_COPY_AFTER_BUILD})

target_sources(SH32Controller PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Params.h
    Source/Pattern.h
    Source/SeqEngine.h)

target_include_directories(SH32Controller PRIVATE Source)

target_compile_definitions(SH32Controller PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SH32Controller
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(SH32_BUILD_TESTS)
    enable_testing()
    add_executable(sh32_engine_test Tests/engine_test.cpp)
    target_include_directories(sh32_engine_test PRIVATE Source)
    add_test(NAME engine COMMAND sh32_engine_test)
endif()

# Tests d'intégration du processeur (application console JUCE, sans hôte audio)
option(SH32_BUILD_HOST_TESTS "Tests d'intégration du processeur et captures de l'éditeur" OFF)
if(SH32_BUILD_HOST_TESTS)
    juce_add_console_app(SH32ProcessorTest PRODUCT_NAME "SH32ProcessorTest")
    target_sources(SH32ProcessorTest PRIVATE
        Tests/ProcessorTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(SH32ProcessorTest PRIVATE Source)
    target_compile_definitions(SH32ProcessorTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SH32ProcessorTest PRIVATE
        juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
