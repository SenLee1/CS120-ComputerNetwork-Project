cmake_minimum_required(VERSION 3.23)
set(CMAKE_CXX_STANDARD 17)

project(AudioNetworkProject VERSION 0.0.1)

# 引入 JUCE
add_subdirectory(../JUCE JUCE)

# 定义公共宏
macro(setup_node name)
    juce_add_gui_app(${name} PRODUCT_NAME ${name})
    juce_generate_juce_header(${name})
    target_link_libraries(${name} PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_graphics
        juce::juce_dsp
    )
    target_include_directories(${name} PRIVATE ${name} Shared)
    # 自动包含源文件
    target_sources(${name} PRIVATE
        ${name}/Main.cpp
        ${name}/MainComponent.cpp
        ${name}/MainComponent.h
        Shared/PacketParams.h
    )
endmacro()

setup_node(Node1)
setup_node(Node2)
