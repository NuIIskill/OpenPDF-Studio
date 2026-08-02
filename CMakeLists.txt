cmake_minimum_required(VERSION 3.20)

# Zentrale Versionsquelle: version.txt im Repo-Root (auch von build.sh und
# packaging/windows/package-win.sh gelesen).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" OPENPDF_VERSION)
string(STRIP "${OPENPDF_VERSION}" OPENPDF_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS version.txt)

project(OpenPDFStudio
    VERSION ${OPENPDF_VERSION}
    DESCRIPTION "OpenPDF Studio - Modern PDF Editor"
    LANGUAGES CXX
)

include(CTest)

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
    src/app/SafeWrite.cpp
    src/app/SafeWrite.hpp
    src/app/SessionStore.cpp
    src/app/SessionStore.hpp

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
    src/engine/edit/DocxLayout.cpp
    src/engine/edit/DocxLayout.hpp
    src/engine/edit/DocumentExporter.cpp
    src/engine/edit/DocumentExporter.hpp

    src/engine/edit/EditSession.cpp
    src/engine/edit/EditSession.hpp

    src/engine/edit/ContentMap.cpp
    src/engine/edit/ContentMap.hpp
    src/engine/edit/ContentModel.cpp
    src/engine/edit/ContentModel.hpp

    src/ui/MainWindow.cpp
    src/ui/MainWindow.hpp
    src/ui/DocumentView.cpp
    src/ui/DocumentView.hpp
    src/ui/PresentationWindow.cpp
    src/ui/PresentationWindow.hpp

    src/ui/bars/TopToolbar.cpp
    src/ui/bars/TopToolbar.hpp
    src/ui/bars/FormatBar.cpp
    src/ui/bars/FormatBar.hpp
    src/ui/bars/StatusBar.cpp
    src/ui/bars/StatusBar.hpp

    src/ui/panels/LeftSidebar.cpp
    src/ui/panels/LeftSidebar.hpp
    src/ui/panels/RightSidebar.cpp
    src/ui/panels/RightSidebar.hpp
    src/ui/panels/ToolPanel.cpp
    src/ui/panels/ToolPanel.hpp
    src/ui/panels/SettingsPanel.cpp
    src/ui/panels/SettingsPanel.hpp
    src/ui/panels/TextPropertiesPanel.cpp
    src/ui/panels/TextPropertiesPanel.hpp

    src/ui/view/PageCanvas.hpp
    src/ui/view/PageLayoutEngine.cpp
    src/ui/view/PageLayoutEngine.hpp
    src/ui/view/ImageAnnotationLayer.cpp
    src/ui/view/ImageAnnotationLayer.hpp
    src/ui/view/TextSelectionController.cpp
    src/ui/view/TextSelectionController.hpp

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

    src/ui/dialogs/ExportDialog.cpp
    src/ui/dialogs/ExportDialog.hpp
    src/ui/dialogs/PdfOrganizerDialog.cpp
    src/ui/dialogs/PdfOrganizerDialog.hpp

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
    configure_file(resources/windows/openpdf-studio.rc.in
                   "${CMAKE_CURRENT_BINARY_DIR}/openpdf-studio.rc" @ONLY)
    target_sources(OpenPDFStudio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/openpdf-studio.rc")
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

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/problem.pdf")
    add_test(NAME docx_export_problem_pdf
        COMMAND ${CMAKE_COMMAND}
            -DAPP=$<TARGET_FILE:OpenPDFStudio>
            -DPDF=${CMAKE_CURRENT_SOURCE_DIR}/tests/problem.pdf
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/problem-regression.docx
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/DocxRegression.cmake
    )
    set_tests_properties(docx_export_problem_pdf PROPERTIES TIMEOUT 120)
endif()

install(FILES packaging/appimage/README.md
    DESTINATION share/doc/OpenPDFStudio
    OPTIONAL
)

# ── CPack: RPM + DEB ─────────────────────────────────────────────────────────
# Gebaut über packaging/linux/package-linux.sh (nicht direkt cpack aufrufen —
# das Skript setzt Prefix/Ausgabepfad und schiebt die Artefakte nach dist/).
if(UNIX AND NOT APPLE)
    set(CPACK_PACKAGE_NAME "openpdf-studio")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VENDOR "nullskill")
    set(CPACK_PACKAGE_CONTACT "nullskill <nullskilll@gmail.com>")
    set(CPACK_PACKAGE_DESCRIPTION
"OpenPDF Studio is a native Qt6 PDF viewer and editor: inline text editing,
page organizer, annotations, OCR and export to PDF, DOCX and image formats.")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://openpdf-studio.nullskill.de")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_PACKAGE_FILE_NAME
        "openpdf-studio-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_STRIP_FILES ON)

    # ── RPM (Fedora/RHEL/openSUSE) ───────────────────────────────────────────
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Productivity")
    set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # Der RPM-Generator übernimmt CPACK_PACKAGE_DESCRIPTION nicht automatisch.
    set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    # rpmbuild leitet Requires automatisch aus den ELF-Sonames ab.
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    # Diese Verzeichnisse gehören filesystem/hicolor-icon-theme — nicht mitpaketieren.
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications
        /usr/share/icons
        /usr/share/icons/hicolor
        /usr/share/icons/hicolor/256x256
        /usr/share/icons/hicolor/256x256/apps
        /usr/share/doc
    )

    # ── DEB (Debian/Ubuntu) ──────────────────────────────────────────────────
    # dpkg-shlibdeps funktioniert beim Bauen auf Fedora nicht (keine Debian-
    # Shlib-Datenbank) → Depends werden explizit gepflegt. Alternativen-Syntax
    # deckt die t64-Umbenennung ab Ubuntu 24.04 mit ab.
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6core6 | libqt6core6t64, libqt6gui6, libqt6widgets6, libqt6printsupport6")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
        "libqt6pdf6, libqpdf29 | libqpdf29t64, libtesseract5 | libtesseract5t64")

    include(CPack)
endif()

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
