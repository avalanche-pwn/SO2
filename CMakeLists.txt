cmake_minimum_required(VERSION 3.28)

project(philosophers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SRCS
    src/main.cpp
)
set(SRCS2
    src/server.cpp
)

add_executable(philosophers ${SRCS})
add_executable(server ${SRCS2})

