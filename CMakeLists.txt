cmake_minimum_required(VERSION 3.21)
project(orpheus VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# build/compile_commands.json : permet à clangd (éditeur) de trouver les en-têtes Qt.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Sql Network Test DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

qt_standard_project_setup(REQUIRES 6.5)

# Cœur réutilisable (testé séparément de l'interface).
add_library(orpheus_core STATIC
    src/Paths.cpp
    src/Track.cpp
    src/Database.cpp
    src/Matcher.cpp
    src/YtDlp.cpp
    src/Downloader.cpp
    src/Spotify.cpp
    src/Lyrics.cpp
    src/LyricsService.cpp
    src/Resolver.cpp
    src/Importer.cpp
    src/Jam.cpp
)
target_include_directories(orpheus_core PUBLIC src)
target_link_libraries(orpheus_core PUBLIC Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network)
target_compile_options(orpheus_core PRIVATE -Wall -Wextra)

qt_add_executable(orpheus
    src/main.cpp
    src/Player.cpp
    src/TrackModel.cpp
    src/Backend.cpp
    src/Mpris.cpp
    src/JamController.cpp
)
target_link_libraries(orpheus PRIVATE orpheus_core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus PkgConfig::MPV)
target_compile_options(orpheus PRIVATE -Wall -Wextra)

# Doit précéder qt_add_qml_module. Les fichiers QML sont placés à la racine du module
# (sinon l'import implicite du dossier qml/ masquerait le singleton OTheme).
set_source_files_properties(qml/OTheme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
foreach(f ${QML_SOURCES})
    get_filename_component(name ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
file(GLOB ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} icons/*.svg)
qt_add_qml_module(orpheus
    URI Orpheus
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/OTheme.qml
        qml/Icon.qml
        qml/IconButton.qml
        qml/PillButton.qml
        qml/Sidebar.qml
        qml/PlayerBar.qml
        qml/TrackList.qml
        qml/SearchView.qml
        qml/PlaylistView.qml
        qml/QueueView.qml
        qml/ImportView.qml
        qml/SettingsView.qml
        qml/Card.qml
        qml/Toasts.qml
        qml/Spinner.qml
        qml/WheelScroll.qml
        qml/LyricsView.qml
        qml/StatsView.qml
        qml/JamView.qml
    RESOURCES ${ICONS}
)

install(TARGETS orpheus RUNTIME DESTINATION bin)

# Tests unitaires du cœur.
enable_testing()
qt_add_executable(orpheus_tests tests/tst_core.cpp)
target_link_libraries(orpheus_tests PRIVATE orpheus_core Qt6::Test)
add_test(NAME core COMMAND orpheus_tests)
