cmake_minimum_required(VERSION 3.20)
project(proxifier CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# WinDivert SDK 解压后的目录，CI 里由 workflow 填充
if(NOT DEFINED WINDIVERT_DIR)
  set(WINDIVERT_DIR "${CMAKE_SOURCE_DIR}/third_party/windivert")
endif()

add_executable(proxifier
  src/main.cpp
  src/config.cpp
  src/targets.cpp
  src/sni.cpp
  src/relay.cpp
  src/engine.cpp
  src/processes.cpp
)

target_include_directories(proxifier PRIVATE
  src
  "${WINDIVERT_DIR}/include"
)

target_link_libraries(proxifier PRIVATE
  ws2_32
  iphlpapi
  "${WINDIVERT_DIR}/x64/WinDivert.lib"
)

target_compile_definitions(proxifier PRIVATE
  _WINSOCK_DEPRECATED_NO_WARNINGS
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

if(MSVC)
  target_compile_options(proxifier PRIVATE /W4 /permissive- /utf-8)
endif()
