qt_add_executable(avena
    main.cpp

    graph/Types.cpp
    graph/Graph.cpp
    graph/NodeCatalog.cpp
    graph/GraphSerializer.cpp

    media/MediaInfo.cpp
    media/ElementProperties.cpp

    engine/PipelineRunner.cpp
    engine/MediaPreview.cpp

    canvas/PortItem.cpp
    canvas/NodeItem.cpp
    canvas/ConnectionItem.cpp
    canvas/NodeEditorScene.cpp
    canvas/NodeEditorView.cpp

    app/PropertiesPanel.cpp
    app/InfoPanel.cpp
    app/NodePalette.cpp
    app/TemplateLibrary.cpp
    app/TemplatePanel.cpp
    app/MainWindow.cpp
)

target_include_directories(avena PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(avena PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

if(AVENA_ENABLE_GSTREAMER)
    target_compile_definitions(avena PRIVATE AVENA_HAVE_GSTREAMER=1)
    target_link_libraries(avena PRIVATE PkgConfig::GST)
endif()

# Treat warnings seriously during development.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(avena PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(avena PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

install(TARGETS avena
    RUNTIME DESTINATION bin)
