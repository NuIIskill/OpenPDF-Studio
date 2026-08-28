target_sources(OpenPDFStudio PRIVATE
    DocumentView.cpp
    DocumentView_Document.cpp
    DocumentView_Edit.cpp
    DocumentView.hpp
    MainWindow.cpp
    MainWindow.hpp
    PresentationWindow.cpp
    PresentationWindow.hpp
)

add_subdirectory(bars)
add_subdirectory(bookmarks)
add_subdirectory(draw)
add_subdirectory(edit)
add_subdirectory(export)
add_subdirectory(history)
add_subdirectory(notes)
add_subdirectory(organizer)
add_subdirectory(panels)
add_subdirectory(settings)
add_subdirectory(theme)
add_subdirectory(tools)
add_subdirectory(view)
add_subdirectory(widgets)
