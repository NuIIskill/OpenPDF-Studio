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
qt_standard_project_setup()

set(SOURCES
    src/main.cpp

    src/app/App.cpp
    src/app/App.hpp
    src/app/AppSettings.cpp
    src/app/AppSettings.hpp

    src/ui/MainWindow.cpp
    src/ui/MainWindow.hpp
    src/ui/DocumentView.cpp
    src/ui/DocumentView.hpp
    src/ui/LeftSidebar.cpp
    src/ui/LeftSidebar.hpp
    src/ui/RightSidebar.cpp
    src/ui/RightSidebar.hpp
    src/ui/TopToolbar.cpp
    src/ui/TopToolbar.hpp
    src/ui/ToolPanel.cpp
    src/ui/ToolPanel.hpp
    src/ui/StatusBar.cpp
    src/ui/StatusBar.hpp

    src/ui/widgets/PagePlaceholder.cpp
    src/ui/widgets/PagePlaceholder.hpp
    src/ui/widgets/ThumbnailItem.cpp
    src/ui/widgets/ThumbnailItem.hpp
    src/ui/widgets/IconButton.cpp
    src/ui/widgets/IconButton.hpp

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

# Wayland platform hint (runtime fallback; real override is in main.cpp)
target_compile_definitions(OpenPDFStudio PRIVATE
    DEFAULT_QPA_PLATFORM="wayland"
)

install(TARGETS OpenPDFStudio
    RUNTIME DESTINATION bin
)

install(FILES packaging/appimage/README.md
    DESTINATION share/doc/OpenPDFStudio
    OPTIONAL
)
