CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude -Isrc
SRCS = src/main.cpp src/MinHeap.cpp src/Graph.cpp src/HashTable.cpp src/Truck.cpp src/GarbageRequest.cpp src/GreedySelection.cpp
TARGET = build/SmartGarbage.exe

all: $(TARGET)

$(TARGET): $(SRCS)
	@if not exist build mkdir build
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) -lws2_32 -pthread

clean:
	@if exist build\SmartGarbage.exe del /Q build\SmartGarbage.exe
