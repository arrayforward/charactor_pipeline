cmake_minimum_required(VERSION 3.16)
project(charactor_pipeline CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(charactor_pipeline
  src/main.cpp
  src/image.cpp
  src/sheet.cpp
  src/matte.cpp
  src/align.cpp
  src/normalmap.cpp
  src/atlas.cpp
  src/cycle.cpp
  src/render.cpp
)

target_include_directories(charactor_pipeline PRIVATE third_party/stb)

if(MSVC)
  target_compile_options(charactor_pipeline PRIVATE /W3)
else()
  target_compile_options(charactor_pipeline PRIVATE -Wall -Wextra -O2)
endif()
