cmake_minimum_required(VERSION 2.8.4)
project(xstream)

find_package(Boost 1.55.0 REQUIRED COMPONENTS system regex thread log filesystem)
include_directories(${Boost_INCLUDE_DIRS})
list(APPEND LIB_LIST ${Boost_LIBRARIES})

# find_package(OpenSSL 1.0.1 REQUIRED)
# include_directories(${OPENSSL_INCLUDE_DIRS})
# list(APPEND LIB_LIST ${OPENSSL_LIBRARIES})

set(SOURCE_FILES
xstream.cpp
)
add_executable(xstream ${SOURCE_FILES})
target_link_libraries(xstream ${LIB_LIST})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

install(PROGRAMS xstream DESTINATION bin)
