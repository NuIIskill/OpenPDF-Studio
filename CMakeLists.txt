cmake_minimum_required(VERSION 3.20)

project(OpenPDFStudio
    VERSION 0.1.0
    DESCRIPTION "OpenPDF Studio - Modern PDF Editor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)
find_package(Qt6 OPTIONAL_COMPONENTS Pdf PrintSupport LinguistTools)
qt_standard_project_setup()

set(SOURCES
    src/main.cpp

    src/engine/ocr/OcrEngine.cpp
    src/engine/ocr/OcrEngine.hpp

    src/app/App.cpp
    src/app/App.hpp
    src/app/AppSettings.cpp
    src/app/AppSettings.hpp

    src/engine/view/PdfRenderer.cpp
    src/engine/view/PdfRenderer.hpp

    src/engine/edit/TextBlock.hpp
    src/engine/edit/PdfTextExtractor.cpp
    src/engine/edit/PdfTextExtractor.hpp
    src/engine/edit/InlineEditor.cpp
    src/engine/edit/InlineEditor.hpp
    src/engine/edit/TextBoxFrame.cpp
    src/engine/edit/TextBoxFrame.hpp
    src/engine/edit/DocxExporter.cpp
    src/engine/edit/DocxExporter.hpp

    src/engine/edit/EditSession.cpp
    src/engine/edit/EditSession.hpp

    src/ui/MainWindow.cpp
    src/ui/MainWindow.hpp
    src/ui/FormatBar.cpp
    src/ui/FormatBar.hpp
    src/ui/DocumentView.cpp
    src/ui/DocumentView.hpp
    src/ui/LeftSidebar.cpp
    src/ui/LeftSidebar.hpp
    src/ui/LeftSidebar.hpp
    src/ui/RightSidebar.cpp
    src/ui/RightSidebar.hpp
    src/ui/TextPropertiesPanel.cpp
    src/ui/TextPropertiesPanel.hpp
    src/ui/TopToolbar.cpp
    src/ui/TopToolbar.hpp
    src/ui/ToolPanel.cpp
    src/ui/ToolPanel.hpp
    src/ui/StatusBar.cpp
    src/ui/StatusBar.hpp
    src/ui/SettingsPanel.cpp
    src/ui/SettingsPanel.hpp

    src/ui/widgets/PagePlaceholder.cpp
    src/ui/widgets/PagePlaceholder.hpp
    src/ui/widgets/ThumbnailItem.cpp
    src/ui/widgets/ThumbnailItem.hpp
    src/ui/widgets/IconButton.cpp
    src/ui/widgets/IconButton.hpp

    src/ui/tools/TextAnnotation.cpp
    src/ui/tools/TextAnnotation.hpp
    src/ui/tools/CommentAnnotation.cpp
    src/ui/tools/CommentAnnotation.hpp
    src/ui/tools/ImageAnnotation.cpp
    src/ui/tools/ImageAnnotation.hpp
    src/ui/tools/UndoCommands.hpp

    src/ui/ExportDialog.cpp
    src/ui/ExportDialog.hpp

    src/ui/organizer/PdfOrganizerDialog.cpp
    src/ui/organizer/PdfOrganizerDialog.hpp

    src/ui/theme/Theme.cpp
    src/ui/theme/Theme.hpp
)

set(RESOURCES
    resources/openpdf-studio.qrc
)

add_executable(OpenPDFStudio
    ${SOURCES}
    ${RESOURCES}
)

target_compile_definitions(OpenPDFStudio PRIVATE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    QT_NO_KEYWORDS
    APP_VERSION="${PROJECT_VERSION}"
)

target_include_directories(OpenPDFStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(OpenPDFStudio PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
)

if(Qt6Pdf_FOUND)
    target_compile_definitions(OpenPDFStudio PRIVATE HAVE_QT_PDF HAVE_PDF_RENDERING)
    target_link_libraries(OpenPDFStudio PRIVATE Qt6::Pdf)
else()
    find_path(POPPLER_QT6_INCLUDE_DIR NAMES poppler/qt6/poppler-qt6.h)
    find_library(POPPLER_QT6_LIBRARY NAMES poppler-qt6)
    if(POPPLER_QT6_INCLUDE_DIR AND POPPLER_QT6_LIBRARY)
        message(STATUS "PDF backend: Poppler-Qt6 (${POPPLER_QT6_LIBRARY})")
        target_compile_definitions(OpenPDFStudio PRIVATE HAVE_POPPLER HAVE_PDF_RENDERING)
        target_include_directories(OpenPDFStudio PRIVATE ${POPPLER_QT6_INCLUDE_DIR})
        target_link_libraries(OpenPDFStudio PRIVATE ${POPPLER_QT6_LIBRARY})
    else()
        message(STATUS "PDF backend: none — install Qt6::Pdf or Poppler-Qt6")
    endif()
endif()

if(Qt6PrintSupport_FOUND)
    target_compile_definitions(OpenPDFStudio PRIVATE HAVE_QT_PRINT)
    target_link_libraries(OpenPDFStudio PRIVATE Qt6::PrintSupport)
endif()

target_include_directories(OpenPDFStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/3rdparty
)

# PkgConfig is used as a fallback for qpdf and Tesseract on native builds only.
# Cross-compilation must not use host pkg-config — it returns Linux headers that
# conflict with the Windows sysroot (e.g. /usr/include/stdint.h vs MinGW types).
if(NOT CMAKE_CROSSCOMPILING)
    find_package(PkgConfig QUIET)
endif()

# ── qpdf: vector PDF content-stream overlay (true text replacement) ───────────
find_package(qpdf QUIET CONFIG)
if(qpdf_FOUND)
    message(STATUS "qpdf ${qpdf_VERSION}: vector PDF save enabled")
    target_compile_definitions(OpenPDFStudio PRIVATE HAVE_QPDF)
    target_link_libraries(OpenPDFStudio PRIVATE qpdf)
elseif(PkgConfig_FOUND)
    pkg_check_modules(QPDF QUIET libqpdf)
    if(QPDF_FOUND)
        message(STATUS "qpdf (pkg-config): vector PDF save enabled")
        target_compile_definitions(OpenPDFStudio PRIVATE HAVE_QPDF)
        target_include_directories(OpenPDFStudio PRIVATE ${QPDF_INCLUDE_DIRS})
        target_link_libraries(OpenPDFStudio PRIVATE ${QPDF_LIBRARIES})
    else()
        message(STATUS "qpdf: not found — sudo dnf install qpdf-devel for vector PDF output")
    endif()
endif()

# ── OCR via Tesseract ─────────────────────────────────────────────────────────
# Optional dependency: install tesseract-devel and tesseract-langpack-deu|eng
if(PkgConfig_FOUND)
    pkg_check_modules(TESSERACT QUIET tesseract)
endif()
if(TESSERACT_FOUND)
    message(STATUS "OCR backend: Tesseract ${TESSERACT_VERSION}")
    target_compile_definitions(OpenPDFStudio PRIVATE HAVE_TESSERACT)
    target_include_directories(OpenPDFStudio PRIVATE ${TESSERACT_INCLUDE_DIRS})
    target_link_libraries(OpenPDFStudio PRIVATE ${TESSERACT_LIBRARIES})
else()
    message(STATUS "OCR backend: none  (sudo dnf install tesseract-devel tesseract-langpack-deu)")
endif()

if(UNIX AND NOT APPLE)
    # Value is hardcoded in main.cpp to avoid CMake semicolon-as-list-separator issues.
    target_compile_definitions(OpenPDFStudio PRIVATE DEFAULT_QPA_PLATFORM)
endif()

if(WIN32)
    set_target_properties(OpenPDFStudio PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(MINGW)
    # MinGW <ratio>/<chrono_io.h> system headers use 'long int' (32-bit, Windows LLP64)
    # for ratio template params, but atto/femto/pico/tera/peta/exa need 64-bit values.
    # GCC reports this as [-Wnarrowing]/[-Wtemplate-body] errors in C++20. Our code is
    # fine — suppress only these system-header diagnostics.
    target_compile_options(OpenPDFStudio PRIVATE -Wno-narrowing -Wno-template-body)
endif()

install(TARGETS OpenPDFStudio
    RUNTIME DESTINATION bin
)

install(FILES packaging/appimage/openpdf-studio.desktop
    DESTINATION share/applications
    OPTIONAL
)

install(FILES resources/icons/openpdf-studio.png
    DESTINATION share/icons/hicolor/256x256/apps
    OPTIONAL
)

install(FILES packaging/appimage/README.md
    DESTINATION share/doc/OpenPDFStudio
    OPTIONAL
)

# Translation update helper — run: cmake --build . --target update_translations
# Pre-compiled .qm files are already embedded via resources/openpdf-studio.qrc.
# This target only updates the .ts source files for translators.
if(Qt6LinguistTools_FOUND AND NOT CMAKE_CROSSCOMPILING)
    set(TS_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_de.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_fr.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_es.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_it.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_pt.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_nl.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_pl.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_ru.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_zh.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_ja.ts
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/openpdf_ko.ts
    )
    qt_add_lupdate(OpenPDFStudio
        TS_FILES ${TS_FILES}
        LUPDATE_TARGET update_translations
        NO_GLOBAL_TARGET
    )
endif()
