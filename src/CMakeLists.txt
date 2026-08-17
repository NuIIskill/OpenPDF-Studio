target_sources(OpenPDFStudio PRIVATE
    App.cpp
    App.hpp
    main.cpp
)

add_subdirectory(app)
add_subdirectory(engine)
add_subdirectory(ui)
