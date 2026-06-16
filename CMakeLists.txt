cmake_minimum_required(VERSION 3.22)

project(ULTRA_Ditherer VERSION 1.0.0 LANGUAGES CXX)

# ── C++ standard ────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Qt auto-tools ────────────────────────────────────────────────────────────
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Find Qt6 ────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Concurrent)

# ── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp

    # Core (pure logic)
    src/core/Params.h
    src/core/ColorMath.h
    src/core/AnimParams.cpp
    src/core/AnimParams.h
    src/core/Animation.cpp
    src/core/Animation.h
    src/core/ImageAdjuster.cpp
    src/core/ImageAdjuster.h
    src/core/HalftoneRenderer.cpp
    src/core/HalftoneRenderer.h
    src/core/GridGenerator.cpp
    src/core/GridGenerator.h
    src/core/DitherRenderer.cpp
    src/core/DitherRenderer.h
    src/core/AsciiRenderer.cpp
    src/core/AsciiRenderer.h
    src/core/BlendCompositor.cpp
    src/core/BlendCompositor.h
    src/core/PaletteStore.cpp
    src/core/PaletteStore.h

    # Workers
    src/workers/RenderWorker.cpp
    src/workers/RenderWorker.h

    # UI
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/PreviewWidget.cpp
    src/ui/PreviewWidget.h
    src/ui/Widgets.cpp
    src/ui/Widgets.h
    src/ui/TonalControlsWidget.cpp
    src/ui/TonalControlsWidget.h
    src/ui/ControlsPanel.cpp
    src/ui/ControlsPanel.h
    src/ui/AdjustmentsPanel.cpp
    src/ui/AdjustmentsPanel.h
    src/ui/ModePanel.cpp
    src/ui/ModePanel.h
    src/ui/FilmstripWidget.cpp
    src/ui/FilmstripWidget.h
    src/ui/TimelineWidget.cpp
    src/ui/TimelineWidget.h
    src/ui/LayersPanel.cpp
    src/ui/LayersPanel.h

    # Resources
    src/resources.qrc
)

# ── Executable ───────────────────────────────────────────────────────────────
if(WIN32)
    add_executable(ULTRA_Ditherer WIN32 ${SOURCES})
else()
    add_executable(ULTRA_Ditherer ${SOURCES})
endif()

# ── Link Qt ──────────────────────────────────────────────────────────────────
target_link_libraries(ULTRA_Ditherer
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
        Qt6::Concurrent
)

# ── Include paths ────────────────────────────────────────────────────────────
target_include_directories(ULTRA_Ditherer PRIVATE src)

# ── Windows: deploy Qt DLLs after build ──────────────────────────────────────
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ULTRA_Ditherer POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-opengl-sw
                    "$<TARGET_FILE:ULTRA_Ditherer>"
            COMMENT "Running windeployqt..."
            VERBATIM
        )
    endif()
endif()

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(ULTRA_Ditherer PRIVATE /W3)
else()
    target_compile_options(ULTRA_Ditherer PRIVATE -Wall -Wextra -Wpedantic)
endif()
