cmake_minimum_required(VERSION 3.21)

project(HandWave VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HANDWAVE_BUILD_APP   "Build the Qt Quick application" ON)
option(HANDWAVE_BUILD_TESTS "Build the core unit tests"      ON)

# ---------------------------------------------------------------------------
# core: pure C++ DSP (no Qt). Shared by the app, the tests, and later the
# LFO / envelope.
# ---------------------------------------------------------------------------
add_library(handwave_core STATIC
    core/Fft.cpp
    core/Fft.h
    core/ParamSmoother.h
    core/ShapeEdits.cpp
    core/ShapeEdits.h
    core/SpscQueue.h
    core/Wavetable.h
    core/WavetableBuilder.cpp
    core/WavetableBuilder.h
    core/WavetableOscillator.cpp
    core/WavetableOscillator.h
)
target_include_directories(handwave_core PUBLIC core)

# ---------------------------------------------------------------------------
# audio: output device via miniaudio (WASAPI on Windows, ALSA on the Pi).
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)

add_library(handwave_audio STATIC
    audio/AudioEngine.cpp
    audio/AudioEngine.h
    audio/MiniaudioImpl.c
)
target_include_directories(handwave_audio PUBLIC audio)
target_include_directories(handwave_audio PRIVATE third_party/miniaudio)
target_link_libraries(handwave_audio PUBLIC handwave_core PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(handwave_audio PRIVATE m)
endif()

# ---------------------------------------------------------------------------
# app: Qt Quick UI
# ---------------------------------------------------------------------------
if(HANDWAVE_BUILD_APP)
    # 6.4 is what Raspberry Pi OS Bookworm ships; don't use newer APIs.
    find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Quick QuickControls2)
    qt_standard_project_setup()

    qt_add_executable(handwave app/main.cpp)

    set_source_files_properties(app/qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
    qt_add_qml_module(handwave
        URI HandWave
        VERSION 1.0
        RESOURCE_PREFIX /
        QML_FILES
            app/qml/Main.qml
        SOURCES
            app/SynthController.cpp
            app/SynthController.h
            app/WaveCanvas.cpp
            app/WaveCanvas.h
    )

    # The generated QML type registration code includes headers by bare name.
    target_include_directories(handwave PRIVATE app)
    target_link_libraries(handwave PRIVATE Qt6::Gui Qt6::Quick Qt6::QuickControls2 handwave_audio)

    set_target_properties(handwave PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)

    install(TARGETS handwave RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# tests
# ---------------------------------------------------------------------------
if(HANDWAVE_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(handwave_tests
        tests/test_builder.cpp
        tests/test_oscillator.cpp
        tests/test_shape_edits.cpp
    )
    target_link_libraries(handwave_tests PRIVATE handwave_audio Catch2::Catch2WithMain)
    if(MINGW)
        # Lets the test binary run without the MinGW runtime DLLs on PATH.
        target_link_options(handwave_tests PRIVATE -static)
    endif()

    enable_testing()
    add_test(NAME handwave_tests COMMAND handwave_tests)
endif()
