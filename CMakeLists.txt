# CMakeLists.txt for MoonUnit
#
# © 2019 by Richard Walters

cmake_minimum_required(VERSION 3.8)
set(This MoonUnit)

set(Headers
    src/Runner.hpp
)

set(Sources
    src/main.cpp
    src/Runner.cpp
)

add_executable(${This} ${Sources} ${Headers})
set_target_properties(${This} PROPERTIES
    FOLDER Applications
)

target_link_libraries(${This} PUBLIC
    LuaLibrary
    SystemAbstractions
)

if(UNIX AND NOT APPLE)
    target_link_libraries(${This} PRIVATE
        -static-libstdc++
    )
endif(UNIX AND NOT APPLE)

add_test(
    NAME ${This}
    COMMAND "${This} --path ../../MoonUnit"
)
