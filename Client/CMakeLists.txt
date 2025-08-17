cmake_minimum_required(VERSION 3.10)

project(Client)

find_package(msquic CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)

add_executable(Client main.cpp)

target_link_libraries(Client PRIVATE msquic)
target_link_libraries(Client PRIVATE opencv_core opencv_imgproc opencv_imgcodecs opencv_highgui)