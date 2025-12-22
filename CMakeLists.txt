cmake_minimum_required(VERSION 3.20)
project(FolderWidget VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows-specific settings
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
    set(CMAKE_WIN32_EXECUTABLE ON)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/FolderWidget.cpp
    src/FolderWidget.h
    src/Config.h
)

# Create executable
add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    gdiplus
    ole32
    shell32
    user32
    gdi32
    comctl32
    dwmapi
    uxtheme
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Copy config file to build directory
configure_file(
    ${CMAKE_SOURCE_DIR}/config/folders.json
    ${CMAKE_BINARY_DIR}/folders.json
    COPYONLY
)
