# Standalone so it needs only Core, Gui (for QImage) and Network -- no Quick,
# no QML module, no platform plugin. Built by probe.sh against Ubuntu's Qt.
cmake_minimum_required(VERSION 3.21)
project(cfb_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network)

add_executable(cfb_probe
    probe.cpp
    ../src/GameFeed.cpp
    ../src/GameFeed.h
    ../src/LogoStore.cpp
    ../src/LogoStore.h
)
target_include_directories(cfb_probe PRIVATE ../src)
target_link_libraries(cfb_probe PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
