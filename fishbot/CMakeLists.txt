cmake_minimum_required(VERSION 3.15)
project(fishbot CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# DPP is usually installed system-wide (libdpp-dev) or built from source.
# If you built DPP from source, point DPP_ROOT at the install prefix, e.g.:
#   cmake -B build -DCMAKE_PREFIX_PATH=/usr/local
find_library(DPP_LIB NAMES dpp)
find_path(DPP_INCLUDE_DIR NAMES dpp/dpp.h)

if(NOT DPP_LIB OR NOT DPP_INCLUDE_DIR)
    message(WARNING "DPP library not found — the 'fishbot' Discord bot target will be "
                     "skipped. 'fishadmin' (the CLI tool) has no DPP dependency and will "
                     "still build fine. Install libdpp-dev, or build DPP from source and "
                     "pass -DCMAKE_PREFIX_PATH=<dpp install prefix>, to also build the bot.")
    set(HAVE_DPP FALSE)
else()
    set(HAVE_DPP TRUE)
endif()

if(HAVE_DPP)
    add_executable(fishbot
        src/main.cpp
        src/Database.cpp
        src/FishData.cpp
        src/Game.cpp
        src/Commands.cpp
    )

    target_include_directories(fishbot PRIVATE
        include
        ${DPP_INCLUDE_DIR}
        ${SQLITE3_INCLUDE_DIRS}
    )

    target_link_libraries(fishbot PRIVATE
        ${DPP_LIB}
        ${SQLITE3_LIBRARIES}
        pthread
    )

    target_compile_options(fishbot PRIVATE -Wall -Wextra -O2)
endif()

# fishadmin: a separate CLI tool for granting coins/gear/fish from the command line.
# Deliberately does NOT link DPP — Database/FishData/Game have no Discord dependency,
# so this builds (and runs) even on a box without DPP installed at all.
add_executable(fishadmin
    src/admin_cli.cpp
    src/Database.cpp
    src/FishData.cpp
    src/Game.cpp
)

target_include_directories(fishadmin PRIVATE
    include
    ${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(fishadmin PRIVATE
    ${SQLITE3_LIBRARIES}
    pthread
)

target_compile_options(fishadmin PRIVATE -Wall -Wextra -O2)
