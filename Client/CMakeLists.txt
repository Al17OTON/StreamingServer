cmake_minimum_required(VERSION 3.10)

project(Client)

find_package(msquic CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nghttp3 CONFIG REQUIRED)

add_executable(Client main.cpp WebCam.cpp QUICClient.cpp ClientConnectionContext.cpp ClientHttp3.cpp)

target_link_libraries(Client PRIVATE msquic)
target_link_libraries(Client PRIVATE opencv_core opencv_imgproc opencv_imgcodecs opencv_highgui)
target_link_libraries(Client PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(Client PRIVATE $<IF:$<TARGET_EXISTS:nghttp3::nghttp3>,nghttp3::nghttp3,nghttp3::nghttp3_static>)