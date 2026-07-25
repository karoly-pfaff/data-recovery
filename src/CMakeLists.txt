# SPDX-License-Identifier: GPL-3.0-or-later
# The shared static core. Frontends and tests link `revenant`; nothing links
# platform code directly (DIP: consumers see interfaces in include/revenant).

add_library(revenant STATIC
    core/ByteReader.cpp
    core/Version.cpp
    core/log/LogLevel.cpp
    core/log/Logger.cpp
    core/log/StderrSink.cpp
)
add_library(revenant::revenant ALIAS revenant)

target_include_directories(revenant PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(revenant PRIVATE REVENANT_VERSION="${PROJECT_VERSION}")
target_link_libraries(revenant PRIVATE revenant_project_options)
