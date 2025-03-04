cmake_minimum_required(VERSION 3.15)
project(CrowSample)

set(INCLUDE_PATHS boost_1_87_0 crow/include)

add_executable(CrowSample src/main.cpp)

target_include_directories(CrowSample PUBLIC ${INCLUDE_PATHS})
