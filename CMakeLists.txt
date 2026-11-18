# Host build: the portable core, unit tests and the screenshot simulator.
# The ESP32 build uses Arduino (see targets/cyd); this file is ignored there.
cmake_minimum_required(VERSION 3.16)
project(stella32 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(STELLA_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" ON)
# Same language subset as the firmware: no exceptions, no RTTI.
set(STELLA_WARNINGS -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wno-missing-field-initializers -fno-exceptions -fno-rtti)
if(STELLA_SANITIZE)
  set(STELLA_SAN -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
endif()

# Portable core: everything under src/ except ports and boards (device-only).
file(GLOB_RECURSE STELLA_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/stella/*.cpp)
list(FILTER STELLA_CORE_SOURCES EXCLUDE REGEX "/src/stella/(port|boards)/")
add_library(stella STATIC ${STELLA_CORE_SOURCES})
target_include_directories(stella PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_options(stella PRIVATE ${STELLA_WARNINGS} ${STELLA_SAN})
target_link_options(stella PUBLIC ${STELLA_SAN})
find_package(Threads REQUIRED)
target_link_libraries(stella PUBLIC Threads::Threads)

# Host port: simulated display/touch/storage/platform.
file(GLOB STELLA_HOST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/host/*.cpp)
list(FILTER STELLA_HOST_SOURCES EXCLUDE REGEX "/host/(sim|bench)_main.cpp$")
add_library(stella_host STATIC ${STELLA_HOST_SOURCES})
target_include_directories(stella_host PUBLIC ${CMAKE_SOURCE_DIR}/host)
# The simulator and tests read the gift build's words (examples/CYD/gift.example.h).
target_include_directories(stella_host PUBLIC ${CMAKE_SOURCE_DIR}/examples/CYD)
target_link_libraries(stella_host PUBLIC stella)
target_compile_options(stella_host PRIVATE ${STELLA_WARNINGS} ${STELLA_SAN})

add_executable(stella_sim ${CMAKE_SOURCE_DIR}/host/sim_main.cpp)
target_link_libraries(stella_sim PRIVATE stella_host)
target_compile_options(stella_sim PRIVATE ${STELLA_WARNINGS} ${STELLA_SAN})

add_executable(stella_bench ${CMAKE_SOURCE_DIR}/host/bench_main.cpp)
target_link_libraries(stella_bench PRIVATE stella_host)
target_compile_options(stella_bench PRIVATE ${STELLA_WARNINGS} ${STELLA_SAN})

enable_testing()
file(GLOB STELLA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
add_executable(stella_tests ${STELLA_TEST_SOURCES})
target_link_libraries(stella_tests PRIVATE stella_host)
target_compile_options(stella_tests PRIVATE ${STELLA_WARNINGS} ${STELLA_SAN})
add_test(NAME stella_tests COMMAND stella_tests)
