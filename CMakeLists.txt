cmake_minimum_required(VERSION 3.16)
project(SmartGarbage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

add_executable(SmartGarbage
    src/main.cpp
    src/MinHeap.cpp
    src/Graph.cpp
    src/HashTable.cpp
    src/Truck.cpp
    src/GarbageRequest.cpp
    src/GreedySelection.cpp
)

if(WIN32)
    target_link_libraries(SmartGarbage ws2_32)
endif()
