cmake_minimum_required(VERSION 3.2)
project(latency-test)

# find all c sources and add them to build
file(GLOB SOURCES "*.c")
add_executable(latency-test ${SOURCES})

# link to ALSA library -lasound
find_package (ALSA)
include_directories(${ALSA_INCLUDE_DIRS})
target_link_libraries (latency-test ${ALSA_LIBRARIES})

# set kernel ftrace
option(FTRACE "use kernel ftrace" OFF)
if(FTRACE)
    add_definitions(-DFTRACE)
endif(FTRACE)
