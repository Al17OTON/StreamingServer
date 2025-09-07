cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(Server)

find_package(FFMPEG REQUIRED)
find_package(msquic CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nghttp3 CONFIG REQUIRED)

add_executable(Server main.cpp QUICServer.cpp ServerConnectionContext.cpp ServerHttp3.cpp)

target_link_libraries(Server PRIVATE msquic)
target_include_directories(Server PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(Server PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(Server PRIVATE ${FFMPEG_LIBRARIES})
target_link_libraries(Server PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(Server PRIVATE $<IF:$<TARGET_EXISTS:nghttp3::nghttp3>,nghttp3::nghttp3,nghttp3::nghttp3_static>)

add_compile_definitions(PROJECT_ROOT="${CMAKE_SOURCE_DIR}")