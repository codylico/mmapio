
cmake_minimum_required(VERSION 3.0)

project(mmapio C)

option(BUILD_TESTING "Enable testing.")
option(BUILD_SHARED_LIBS "Enable shared library construction.")
set(MMAPIO_OS CACHE STRING "Target memory mapping API.")

add_library(mmapio "mmapio.c" "mmapio.h")
if (MMAPIO_OS GREATER -1)
  target_compile_definitions(mmapio
    PRIVATE "MMAPIO_OS=${MMAPIO_OS}")
endif (MMAPIO_OS GREATER -1)
if (WIN32 AND BUILD_SHARED_LIBS)
  target_compile_definitions(mmapio
    PUBLIC "MMAPIO_WIN32_DLL")
endif(WIN32 AND BUILD_SHARED_LIBS)

if (BUILD_TESTING)
  add_executable(mmapio_dump "tests/dump.c")
  target_link_libraries(mmapio_dump mmapio)

  add_executable(mmapio_config "tests/config.c")
  target_link_libraries(mmapio_config mmapio)
endif (BUILD_TESTING)

