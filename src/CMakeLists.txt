target_sources(OpenPDFStudio PRIVATE
    App.cpp
    App.hpp
    main.cpp
)

add_subdirectory(3rdparty)
add_subdirectory(app)
add_subdirectory(cli)
add_subdirectory(drm)
add_subdirectory(engine)
add_subdirectory(ui)
