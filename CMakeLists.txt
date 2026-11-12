cmake_minimum_required(VERSION 3.21)

project(beater VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Windows (MSVC): M_PI from <cmath>, no min / max macros from <windows.h>,
# UTF-8 sources, and Rubber Band's one big translation unit needs /bigobj.
if(MSVC)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
    add_compile_options(/utf-8)
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# Third party: miniaudio (audio I/O + fallback decoding), Rubber Band (stretch/pitch)
# ---------------------------------------------------------------------------
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE)
FetchContent_Declare(rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG v4.0.0
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()
FetchContent_GetProperties(rubberband)
if(NOT rubberband_POPULATED)
    FetchContent_Populate(rubberband)
endif()

add_library(miniaudio STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
target_include_directories(miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
target_compile_definitions(miniaudio PUBLIC MA_NO_ENGINE MA_NO_NODE_GRAPH MA_NO_RESOURCE_MANAGER MA_NO_GENERATION)
if(APPLE)
    target_link_libraries(miniaudio PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(miniaudio PUBLIC m)
    endif()
endif()

add_library(rubberband STATIC ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp)
target_include_directories(rubberband SYSTEM PUBLIC ${rubberband_SOURCE_DIR})
if(APPLE)
    target_link_libraries(rubberband PUBLIC "-framework Accelerate")
endif()
if(MSVC)
    target_compile_options(rubberband PRIVATE /bigobj)
endif()

# ---------------------------------------------------------------------------
# beater_core: audio engine + analysis, no Qt (so it can be tested headless)
# ---------------------------------------------------------------------------
add_library(beater_core STATIC
    src/core/SpscQueue.h
    src/core/TrackBuffer.h
    src/core/BeatGrid.h
    src/core/Key.h
    src/audio/Decoder.h
    src/audio/Decoder.cpp
    src/audio/DeckEngine.h
    src/audio/DeckEngine.cpp
    src/audio/AudioEngine.h
    src/audio/AudioEngine.cpp
    src/analysis/Fft.h
    src/analysis/TempoEstimator.h
    src/analysis/StructureEstimator.h
    src/analysis/StructureEstimator.cpp
    src/analysis/TempoEstimator.cpp
    src/analysis/KeyEstimator.h
    src/analysis/KeyEstimator.cpp
)
target_include_directories(beater_core PUBLIC src)
target_link_libraries(beater_core PUBLIC miniaudio rubberband)
if(APPLE)
    target_link_libraries(beater_core PUBLIC "-framework AudioToolbox" "-framework CoreFoundation")
endif()

# ---------------------------------------------------------------------------
# App
# ---------------------------------------------------------------------------
option(BEATER_CORE_ONLY "Only build the audio core and tests (no Qt)" OFF)
if(NOT BEATER_CORE_ONLY)
find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Concurrent)
qt_standard_project_setup(REQUIRES 6.5)

# UI kit (git submodule: git submodule update --init)
add_subdirectory(external/highlighter-ui/HighlighterUI ${CMAKE_BINARY_DIR}/HighlighterUI)

qt_add_executable(beater
    src/main.cpp
)
# qmltyperegistrar includes the QML_ELEMENT headers by bare file name
target_include_directories(beater PRIVATE src/app)
set(BEATER_QML
    qml/Main.qml
    qml/Look.qml
    qml/Panel.qml
    qml/MasterTempo.qml
    qml/BeatLights.qml
    qml/ModeSwitch.qml
    qml/TopBar.qml
    qml/EditView.qml
    qml/DeckView.qml
    qml/MixView.qml
    qml/PerformView.qml
    qml/PerformDeck.qml
    qml/LibraryPanel.qml
    qml/DeckDrop.qml
    qml/LoopLane.qml
    qml/Crossfader.qml
    qml/DeckClock.qml
    qml/PerformStrip.qml
    qml/DeckBeat.qml
    qml/AutoDjButton.qml
    qml/AutoDjStatus.qml
)
# Put the QML files at the module root so they see the module's C++ types.
foreach(f IN LISTS BEATER_QML)
    get_filename_component(n ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${n})
endforeach()
set_source_files_properties(qml/Look.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(beater
    URI Beater
    VERSION 1.0
    QML_FILES ${BEATER_QML}
    SOURCES
        src/app/TrackRecord.h
        src/app/Library.h
        src/app/Library.cpp
        src/app/DeckController.h
        src/app/DeckController.cpp
        src/app/AutoDj.h
        src/app/AutoDj.cpp
        src/app/AppController.h
        src/app/AppController.cpp
        src/app/WaveformItem.h
        src/app/WaveformItem.cpp
)

set_target_properties(beater PROPERTIES
    WIN32_EXECUTABLE TRUE   # a window, no console, on Windows
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER nl.blocksystem.beater
    MACOSX_BUNDLE_BUNDLE_NAME beater
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

target_link_libraries(beater PRIVATE beater_core HighlighterUIplugin Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent)
endif()

# ---------------------------------------------------------------------------
# Headless tests (engine sync / loops / tempo estimate)
# ---------------------------------------------------------------------------
option(BEATER_TESTS "Build headless engine tests" ON)
if(BEATER_TESTS)
    add_executable(engine_test tests/engine_test.cpp)
    target_link_libraries(engine_test PRIVATE beater_core)
endif()
