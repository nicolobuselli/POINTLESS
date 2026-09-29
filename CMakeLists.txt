cmake_minimum_required(VERSION 3.22)

project(POINTLESS VERSION 2.0.0 LANGUAGES CXX)

# ── C++ standard ────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Qt auto-tools ────────────────────────────────────────────────────────────
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Find Qt6 ────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Concurrent ShaderTools)

# ── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES


    # Core (pure logic)
    src/core/Params.h
    src/core/ColorMath.h
    src/core/CellSample.h
    src/core/Parallel.h
    src/core/AnimParams.cpp
    src/core/AnimParams.h
    src/core/Animation.cpp
    src/core/Animation.h
    src/core/ImageAdjuster.cpp
    src/core/ImageAdjuster.h
    src/core/DotGridRenderer.cpp
    src/core/DotGridRenderer.h
    src/core/HalftoneRenderer.cpp
    src/core/HalftoneRenderer.h
    src/core/GridGenerator.cpp
    src/core/GridGenerator.h
    src/core/DitherRenderer.cpp
    src/core/DitherRenderer.h
    src/core/AsciiRenderer.cpp
    src/core/AsciiRenderer.h
    src/core/MosaicRenderer.cpp
    src/core/MosaicRenderer.h
    src/core/BlendCompositor.cpp
    src/core/BlendCompositor.h
    src/core/PaletteStore.cpp
    src/core/PaletteStore.h
    src/core/ProjectIO.cpp
    src/core/ProjectIO.h
    src/core/VideoIO.cpp
    src/core/FrameStore.h
    src/core/VideoIO.h

    # Workers
    src/workers/RenderWorker.cpp
    src/workers/RenderWorker.h

    # GPU
    src/gpu/GpuFramePackage.h

    # UI
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/GpuCanvasWidget.cpp
    src/ui/GpuCanvasWidget.h
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

add_library(pointless_app OBJECT ${SOURCES})
target_include_directories(pointless_app PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(src/Version.h.in generated/Version.h @ONLY)
configure_file(src/app.rc.in generated/app.rc @ONLY)

# ── Executable ───────────────────────────────────────────────────────────────
if(WIN32)
    add_executable(POINTLESS WIN32 src/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc")
    # app.rc.obj non dipende da logo.ico secondo make: senza questo, sostituire
    # logo.ico non forza il recompile della risorsa e l'exe tiene l'icona vecchia.
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc" PROPERTIES OBJECT_DEPENDS
        "${CMAKE_SOURCE_DIR}/assets/logo.ico")
else()
    add_executable(POINTLESS src/main.cpp)
endif()

# ── GPU shaders (baked to portable .qsb via Qt ShaderTools) ─────────────────
# GLSL floor raised to 300es/330: dot.vert folds a uint constant ≥ 2^31
# (PCG hash increment) that the default 100es/120 targets cannot express.
qt_add_shaders(pointless_app "pointless_shaders"
    PREFIX "/shaders"
    BASE "src/gpu/shaders"
    GLSL "300es,330"
    FILES
        src/gpu/shaders/blit.vert
        src/gpu/shaders/blit.frag
        src/gpu/shaders/composite.vert
        src/gpu/shaders/composite.frag
        src/gpu/shaders/dot.vert
        src/gpu/shaders/dot.frag
        src/gpu/shaders/halftone.vert
        src/gpu/shaders/halftone.frag
        src/gpu/shaders/adjust.vert
        src/gpu/shaders/adjust.frag
        src/gpu/shaders/dither.vert
        src/gpu/shaders/dither.frag
        src/gpu/shaders/mosaic.vert
        src/gpu/shaders/mosaic.frag
        src/gpu/shaders/ascii.vert
        src/gpu/shaders/ascii.frag
        src/gpu/shaders/ascii_grid.vert
        src/gpu/shaders/ascii_grid.frag
)

# ── Link Qt ──────────────────────────────────────────────────────────────────
target_link_libraries(pointless_app
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
        Qt6::Concurrent
)

target_link_libraries(POINTLESS PRIVATE pointless_app)

# Shell32: SHChangeNotify (main.cpp's .less file-association registration).
if(WIN32)
    target_link_libraries(POINTLESS PRIVATE Shell32)
endif()

# ── Include paths ────────────────────────────────────────────────────────────
target_include_directories(POINTLESS PRIVATE src)

# QRhi "semi-public" headers (rhi/qrhi.h) live in QtGui's versioned private
# include dirs; this kit exports neither a Qt6::GuiPrivate target nor a
# populated Qt6Gui_PRIVATE_INCLUDE_DIRS, so derive the paths from Qt6_DIR.
get_filename_component(_qt_include_root "${Qt6_DIR}/../../../include" ABSOLUTE)
target_include_directories(pointless_app PUBLIC
    "${_qt_include_root}/QtGui/${Qt6_VERSION}"
    "${_qt_include_root}/QtGui/${Qt6_VERSION}/QtGui"
)

# ── Windows: deploy Qt DLLs after build ──────────────────────────────────────
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET POINTLESS POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-opengl-sw
                    "$<TARGET_FILE:POINTLESS>"
            COMMENT "Running windeployqt..."
            VERBATIM
        )
    endif()

    # Bundle ffmpeg.exe next to the app so video import/export works out of the box.
    if(EXISTS "${CMAKE_SOURCE_DIR}/tools/ffmpeg.exe")
        add_custom_command(TARGET POINTLESS POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_SOURCE_DIR}/tools/ffmpeg.exe"
                    "$<TARGET_FILE_DIR:POINTLESS>/ffmpeg.exe"
            COMMENT "Bundling ffmpeg.exe..."
            VERBATIM
        )
    endif()
endif()

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(pointless_app PRIVATE /W3 /Zi)
    target_compile_options(POINTLESS PRIVATE /W3 /Zi)
    target_link_options(POINTLESS PRIVATE /DEBUG)
else()
    target_compile_options(pointless_app PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(POINTLESS PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET POINTLESS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES.txt" "$<TARGET_FILE_DIR:POINTLESS>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:POINTLESS>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/fonts/OFL.txt" "$<TARGET_FILE_DIR:POINTLESS>/licenses/Funnel-OFL.txt"
    VERBATIM)
include(CTest)
if(BUILD_TESTING)
    target_compile_definitions(pointless_app PUBLIC POINTLESS_TESTING)
    foreach(check document_state_check export_frame_check audit_regression_check)
        add_executable(${check} tests/${check}.cpp)
        target_link_libraries(${check} PRIVATE pointless_app)
        add_test(NAME ${check} COMMAND ${check})
        set_tests_properties(${check} PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${_qt_bin_dir}/../plugins/platforms")
        if(WIN32)
            set_tests_properties(${check} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
        endif()
    endforeach()
    add_executable(nearest_glyph_check tests/nearest_glyph_check.cpp)
    add_test(NAME nearest_glyph_check COMMAND nearest_glyph_check)
endif()

if(BUILD_TESTING AND WIN32)
    add_executable(gpu_render_check tests/gpu_render_check.cpp)
    target_link_libraries(gpu_render_check PRIVATE pointless_app)
    add_test(NAME gpu_render_check COMMAND gpu_render_check)
    set_tests_properties(gpu_render_check PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=${_qt_bin_dir}/../plugins/platforms"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()
