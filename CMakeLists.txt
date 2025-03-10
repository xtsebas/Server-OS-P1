cmake_minimum_required(VERSION 3.15)
project(CrowSample)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED)

add_definitions(
    -DCROW_ENABLE_BOOST_ASIO      
    -DBOOST_ASIO_NO_DEPRECATED
    -DBOOST_ASIO_HEADER_ONLY
)

include_directories(${Boost_INCLUDE_DIRS})

set(CROW_DIR "${CMAKE_SOURCE_DIR}/crow")
include_directories(${CROW_DIR}/include)

add_executable(CrowSample src/main.cpp)

target_include_directories(CrowSample PUBLIC 
    ${Boost_INCLUDE_DIRS}
    ${CROW_DIR}/include
)
