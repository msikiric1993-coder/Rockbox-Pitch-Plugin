cmake_minimum_required(VERSION 3.22)
project(TDSpeedVST VERSION 0.4.2 LANGUAGES CXX)

add_subdirectory(JUCE)

juce_add_plugin(TDSpeedVST
    COMPANY_NAME "Fyodor"
    BUNDLE_ID    "com.fyodor.tdspeedpreview"
    IS_SYNTH     FALSE
    NEEDS_MIDI_INPUT  FALSE
    NEEDS_MIDI_OUTPUT FALSE
    FORMATS      VST3
    PRODUCT_NAME "TDSpeedPreview"
    PLUGIN_MANUFACTURER_CODE Fyod
    PLUGIN_CODE TdP1)

target_sources(TDSpeedVST PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/FileRender.cpp
    Source/EffectsRack.cpp
    Source/EffectsPanel.cpp)

file(GLOB AIRWINDOWS_SOURCES CONFIGURE_DEPENDS Source/ThirdParty/Airwindows/src/autogen_airwin/*.cpp)
set(FREEVERB_SOURCES)
foreach(name allpass biquad comb delay delayline earlyref efilter nrev nrevb progenitor progenitor2 revbase slot strev utils zrev zrev2)
    list(APPEND FREEVERB_SOURCES Source/ThirdParty/Dragonfly/common/freeverb/${name}.cpp)
endforeach()
add_library(TDSpeedEffectsEngines STATIC ${AIRWINDOWS_SOURCES} ${FREEVERB_SOURCES}
    Source/ThirdParty/Airwindows/src/airwin_consolidated_base.cpp
    Source/ThirdParty/CloudSeed/DSP/Biquad.cpp
    Source/ThirdParty/CloudSeed/DSP/RandomBuffer.cpp
    Source/ThirdParty/CloudSeed/Parameters.cpp
    Source/ThirdParty/ChowMatrix/ChowMatrix-Diffusion.cpp
    Source/ThirdParty/ChowTape/AnalogTapeModel-HysteresisProcessing.cpp)
target_compile_features(TDSpeedEffectsEngines PRIVATE cxx_std_17)
target_include_directories(TDSpeedEffectsEngines PRIVATE JUCE/modules
    Source/ThirdParty/Dragonfly/common)
target_compile_definitions(TDSpeedEffectsEngines PRIVATE LIBFV3_FLOAT _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES BUFFER_SIZE=256 MAX_STR_SIZE=32 JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    $<TARGET_PROPERTY:TDSpeedVST,COMPILE_DEFINITIONS>)
target_include_directories(TDSpeedVST PRIVATE Source/ThirdParty/Dragonfly/common Source/ThirdParty/SignalsmithBasics/include Source/ThirdParty/SignalsmithDSP/include)
target_compile_definitions(TDSpeedVST PRIVATE LIBFV3_FLOAT BUFFER_SIZE=256 MAX_STR_SIZE=32)
target_link_libraries(TDSpeedVST PRIVATE TDSpeedEffectsEngines)

target_compile_features(TDSpeedVST PRIVATE cxx_std_17)

target_link_libraries(TDSpeedVST PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp)

juce_generate_juce_header(TDSpeedVST)

# We only ship VST3; avoid VST2/VST3 migration warning
target_compile_definitions(TDSpeedVST PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)

option(TDSPEED_BUILD_TESTS "Build processor and editor regression checks" OFF)
if(TDSPEED_BUILD_TESTS)
    enable_testing()
    add_executable(TDSpeedTests Tests/PluginTests.cpp)
    target_compile_features(TDSpeedTests PRIVATE cxx_std_17)
    target_include_directories(TDSpeedTests PRIVATE Source JUCE/modules "${CMAKE_CURRENT_BINARY_DIR}/TDSpeedVST_artefacts/JuceLibraryCode")
    target_compile_definitions(TDSpeedTests PRIVATE $<TARGET_PROPERTY:TDSpeedVST,COMPILE_DEFINITIONS>)
    target_link_libraries(TDSpeedTests PRIVATE TDSpeedVST)
    add_test(NAME TDSpeedRegression COMMAND TDSpeedTests)
    add_executable(TDSpeedEffectsTests Tests/EffectsTests.cpp)
    target_compile_features(TDSpeedEffectsTests PRIVATE cxx_std_17)
    target_include_directories(TDSpeedEffectsTests PRIVATE Source JUCE/modules "${CMAKE_CURRENT_BINARY_DIR}/TDSpeedVST_artefacts/JuceLibraryCode")
    target_compile_definitions(TDSpeedEffectsTests PRIVATE $<TARGET_PROPERTY:TDSpeedVST,COMPILE_DEFINITIONS>)
    target_link_libraries(TDSpeedEffectsTests PRIVATE TDSpeedVST)
    add_test(NAME TDSpeedEffectsRegression COMMAND TDSpeedEffectsTests)
endif()

# The existing plugin shared-code library is the common processor/editor core.
# The standalone links it directly and never loads a VST plugin at runtime.
add_library(TDSpeedCore ALIAS TDSpeedVST)
if(WIN32)
    juce_add_binary_data(TDSpeedAssets SOURCES Source/Assets/TDSpeed.png)
    juce_add_gui_app(TDSpeedApp ICON_BIG Source/Assets/TDSpeed.png ICON_SMALL Source/Assets/TDSpeed.png PRODUCT_NAME "TDSpeedPreview" BUNDLE_ID "com.fyodor.tdspeedpreview.app" COMPANY_NAME "Fyodor" VERSION "${PROJECT_VERSION}")
    target_sources(TDSpeedApp PRIVATE Source/Standalone/Main.cpp Source/Standalone/WindowsAudio.cpp Source/Standalone/WasapiEngine.cpp)
    target_compile_features(TDSpeedApp PRIVATE cxx_std_17)
    target_include_directories(TDSpeedApp PRIVATE Source JUCE/modules "${CMAKE_CURRENT_BINARY_DIR}/TDSpeedVST_artefacts/JuceLibraryCode")
    target_compile_definitions(TDSpeedApp PRIVATE $<TARGET_PROPERTY:TDSpeedVST,COMPILE_DEFINITIONS> JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(TDSpeedApp PRIVATE TDSpeedCore TDSpeedAssets ole32 uuid propsys avrt shell32 setupapi)
endif()

if(TDSPEED_BUILD_TESTS)
    add_executable(TDSpeedDelayTests Tests/DelayTests.cpp)
    target_include_directories(TDSpeedDelayTests PRIVATE Source)
    target_compile_features(TDSpeedDelayTests PRIVATE cxx_std_17)
    add_test(NAME TDSpeedDelayBoundary COMMAND TDSpeedDelayTests)
endif()
option(TDSPEED_ENABLE_ASAN "Instrument shared DSP and tests with AddressSanitizer" OFF)
if(TDSPEED_ENABLE_ASAN AND MSVC)
    foreach(target TDSpeedEffectsEngines TDSpeedVST TDSpeedApp TDSpeedTests TDSpeedEffectsTests TDSpeedDelayTests TDSpeedVST_VST3)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
            target_link_options(${target} PRIVATE /INCREMENTAL:NO /DEBUG)
        endif()
    endforeach()
endif()


if(TDSPEED_BUILD_TESTS)
    add_executable(TDSpeedEngineAudit Tests/EngineAudit.cpp)
    target_compile_features(TDSpeedEngineAudit PRIVATE cxx_std_17)
    target_include_directories(TDSpeedEngineAudit PRIVATE Source JUCE/modules "${CMAKE_CURRENT_BINARY_DIR}/TDSpeedVST_artefacts/JuceLibraryCode")
    target_compile_definitions(TDSpeedEngineAudit PRIVATE $<TARGET_PROPERTY:TDSpeedVST,COMPILE_DEFINITIONS>)
    target_link_libraries(TDSpeedEngineAudit PRIVATE TDSpeedVST)
    if(TDSPEED_ENABLE_ASAN AND MSVC)
        target_compile_options(TDSpeedEngineAudit PRIVATE /fsanitize=address /Zi)
        target_link_options(TDSpeedEngineAudit PRIVATE /INCREMENTAL:NO /DEBUG)
    endif()
    add_test(NAME TDSpeedWetEngineAudit COMMAND TDSpeedEngineAudit "${CMAKE_CURRENT_BINARY_DIR}/audit")
    # Load the actual distributable module through an independent JUCE host.
    add_executable(TDSpeedPluginHostSmoke Tests/PluginHostSmoke.cpp)
    target_compile_features(TDSpeedPluginHostSmoke PRIVATE cxx_std_17)
    target_compile_definitions(TDSpeedPluginHostSmoke PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(TDSpeedPluginHostSmoke PRIVATE juce::juce_audio_processors juce::juce_gui_extra)
    add_dependencies(TDSpeedPluginHostSmoke TDSpeedVST_VST3)
    if(TDSPEED_ENABLE_ASAN AND MSVC)
        target_compile_options(TDSpeedPluginHostSmoke PRIVATE /fsanitize=address /Zi)
        target_link_options(TDSpeedPluginHostSmoke PRIVATE /INCREMENTAL:NO /DEBUG)
    endif()
    add_test(NAME TDSpeedActualVST3 COMMAND TDSpeedPluginHostSmoke "${CMAKE_CURRENT_BINARY_DIR}/TDSpeedVST_artefacts/Release/VST3/TDSpeedPreview.vst3")
    add_test(NAME TDSpeedExportTailVST3 COMMAND TDSpeedPluginHostSmoke "${CMAKE_CURRENT_BINARY_DIR}/TDSpeedVST_artefacts/Release/VST3/TDSpeedPreview.vst3" --export-tail)
    add_executable(TDSpeedDawTests Tests/DawTests.cpp)
    target_compile_features(TDSpeedDawTests PRIVATE cxx_std_17)
    target_include_directories(TDSpeedDawTests PRIVATE Source JUCE/modules "${CMAKE_CURRENT_BINARY_DIR}/TDSpeedVST_artefacts/JuceLibraryCode")
    target_compile_definitions(TDSpeedDawTests PRIVATE $<TARGET_PROPERTY:TDSpeedVST,COMPILE_DEFINITIONS>)
    target_link_libraries(TDSpeedDawTests PRIVATE TDSpeedVST)
    if(TDSPEED_ENABLE_ASAN AND MSVC)
        target_compile_options(TDSpeedDawTests PRIVATE /fsanitize=address /Zi)
        target_link_options(TDSpeedDawTests PRIVATE /INCREMENTAL:NO /DEBUG)
    endif()
    add_test(NAME TDSpeedDawSynchronization COMMAND TDSpeedDawTests)
endif()

if(TDSPEED_BUILD_TESTS)
    add_executable(TDSpeedFileRenderTests Tests/FileRenderTests.cpp)
    target_compile_features(TDSpeedFileRenderTests PRIVATE cxx_std_17)
    target_include_directories(TDSpeedFileRenderTests PRIVATE Source JUCE/modules "${CMAKE_CURRENT_BINARY_DIR}/TDSpeedVST_artefacts/JuceLibraryCode")
    target_compile_definitions(TDSpeedFileRenderTests PRIVATE $<TARGET_PROPERTY:TDSpeedVST,COMPILE_DEFINITIONS>)
    target_link_libraries(TDSpeedFileRenderTests PRIVATE TDSpeedVST)
    if(TDSPEED_ENABLE_ASAN AND MSVC)
        target_compile_options(TDSpeedFileRenderTests PRIVATE /fsanitize=address /Zi)
        target_link_options(TDSpeedFileRenderTests PRIVATE /INCREMENTAL:NO /DEBUG)
    endif()
    add_test(NAME TDSpeedFileRender COMMAND TDSpeedFileRenderTests)
endif()
