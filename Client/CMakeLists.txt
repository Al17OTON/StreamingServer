cmake_minimum_required(VERSION 3.10)

project(Client)

find_package(fmt CONFIG REQUIRED)
find_package(msquic CONFIG REQUIRED)

add_executable(Client main.cpp)

target_link_libraries(Client PRIVATE fmt::fmt)
target_link_libraries(Client PRIVATE msquic)