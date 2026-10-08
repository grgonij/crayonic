cmake_minimum_required(VERSION 3.22)

# Bake the C++ runtime into the plugin, so people you share it with don't need
# the Visual C++ Redistributable installed
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(MyPlugin VERSION 0.1.0)

add_subdirectory(JUCE)

juce_add_plugin(MyPlugin
    COMPANY_NAME "prodbyscy_"
    PLUGIN_MANUFACTURER_CODE Ynme
    PLUGIN_CODE Mypl
    IS_SYNTH TRUE                 # instrument (shows up under Generators in FL)
    NEEDS_MIDI_INPUT TRUE         # receives notes from the piano roll
    NEEDS_MIDI_OUTPUT FALSE
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "TEST")

target_sources(MyPlugin PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/FxView.cpp
    Source/Sketch.cpp)

# Handwriting font, compiled into the plugin (SIL Open Font License, see Assets/OFL.txt)
juce_add_binary_data(PluginAssets SOURCES Assets/PatrickHand-Regular.ttf)

target_compile_definitions(MyPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MyPlugin
    PRIVATE
        PluginAssets
        juce::juce_audio_utils
        juce::juce_dsp            # filters, reverb, delay, FFT for the effects stage
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)