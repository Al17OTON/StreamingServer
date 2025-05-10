cmake_minimum_required(VERSION 3.10)

project(Server)

find_package(fmt CONFIG REQUIRED)
find_package(msquic CONFIG REQUIRED)

<<<<<<< Updated upstream
add_executable(Server main.cpp)
=======
add_executable(Server main.cpp VideoEncoder.cpp QUICServer.cpp)
>>>>>>> Stashed changes

target_link_libraries(Server PRIVATE fmt::fmt)
target_link_libraries(Server PRIVATE msquic)