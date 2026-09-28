cmake_minimum_required(VERSION 3.21)
project(cfb_scoreboard VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick Network)
qt_standard_project_setup()

# --- desktop preview / fullscreen build ------------------------------------
qt_add_executable(cfb_scoreboard
    src/main.cpp
    src/GameFeed.cpp
    src/GameFeed.h
    src/EspnUserAgent.h
    src/League.h
    src/WakeTimer.h
    src/Orientation.cpp
    src/Orientation.h
    src/LogoStore.cpp
    src/LogoStore.h
)

# The view lives in ui/ and is shared byte-for-byte with the AppLoad frontend,
# which loads it from resources.rcc. Alias every file to the module root so a
# component in ui/ and the shell in qml/ resolve each other as siblings, the
# same way they do inside the rcc bundle.
set(SCOREBOARD_UI
    ui/Board.qml
    ui/FieldStrip.qml
    ui/LineScore.qml
    ui/LiveList.qml
    ui/MatchupCard.qml
    ui/ScheduleRow.qml
    ui/Slate.qml
    ui/StatusStrip.qml
    ui/TeamPicker.qml
    ui/TeamLogo.qml
    ui/TimeoutRow.qml
)
foreach(f ${SCOREBOARD_UI} qml/Preview.qml)
    get_filename_component(base ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${base})
endforeach()

qt_add_qml_module(cfb_scoreboard
    URI Scoreboard
    VERSION 1.0
    # Pin the prefix so the qrc path is identical on Qt 6.4 (desktop preview)
    # and Qt 6.5+ (the device SDK).
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        qml/Preview.qml
        ${SCOREBOARD_UI}
)

target_link_libraries(cfb_scoreboard PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network)

install(TARGETS cfb_scoreboard RUNTIME DESTINATION bin)

# --- AppLoad backend -------------------------------------------------------
# A console process: no GUI, because the frontend is QML running inside
# xochitl. This exists so the network work happens somewhere with its own
# environment. Qt6::Gui is linked for QImage only -- ESPN ships raster logos
# and they are converted to greyscale here; QImage works fine under
# QCoreApplication with no display.
add_executable(football_backend
    src/backend_main.cpp
    src/AppLoadLink.cpp
    src/AppLoadLink.h
    src/GameFeed.cpp
    src/GameFeed.h
    src/EspnUserAgent.h
    src/League.h
    src/WakeTimer.h
    src/LogoStore.cpp
    src/LogoStore.h
)
target_link_libraries(football_backend PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
