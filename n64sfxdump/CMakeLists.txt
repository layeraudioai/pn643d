cmake_minimum_required(VERSION 3.16)

# Prefer clang if it is on PATH and the user hasn't already chosen a compiler.
# (Must run before project().)
if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
  find_program(CLANGXX NAMES clang++ clang++-18 clang++-17 clang++-16 clang++-15)
  if(CLANGXX)
    set(CMAKE_CXX_COMPILER "${CLANGXX}" CACHE STRING "C++ compiler" FORCE)
  endif()
endif()

project(n64sfxdump CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_executable(n64sfxdump
  src/main.cpp
  src/rom.cpp
  src/wav.cpp
  src/vadpcm.cpp
  src/albank.cpp
)

# std::filesystem needs libstdc++fs on some older GCCs.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(n64sfxdump stdc++fs)
endif()

install(TARGETS n64sfxdump RUNTIME DESTINATION bin)
