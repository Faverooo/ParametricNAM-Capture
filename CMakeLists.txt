cmake_minimum_required(VERSION 3.22)
project(ParametricNAMCapture VERSION 0.1.0 LANGUAGES C CXX)

set(PNAM_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Local JUCE source directory")
if(EXISTS "${PNAM_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${PNAM_JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce")
else()
    find_package(JUCE 8 CONFIG QUIET)
    if(NOT TARGET juce::juce_audio_utils)
        include(FetchContent)
        FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                             GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()
endif()

juce_add_gui_app(ParametricNAMCapture PRODUCT_NAME "ParametricNAM Capture"
                 BUNDLE_ID "com.faverooo.parametricnam.capture"
                 MICROPHONE_PERMISSION_ENABLED TRUE
                 MICROPHONE_PERMISSION_TEXT "Capture the amplifier microphone and audio interface loopback.")
target_sources(ParametricNAMCapture PRIVATE Source/Main.cpp Source/Capture.cpp Source/Session.cpp)
target_compile_features(ParametricNAMCapture PRIVATE cxx_std_20)
target_compile_definitions(ParametricNAMCapture PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ParametricNAMCapture PRIVATE juce::juce_audio_utils)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(JACK QUIET jack)
        if(JACK_FOUND)
            target_compile_definitions(ParametricNAMCapture PRIVATE JUCE_JACK=1)
            target_include_directories(ParametricNAMCapture PRIVATE ${JACK_INCLUDE_DIRS})
        endif()
    endif()
endif()
