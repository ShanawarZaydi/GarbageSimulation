# Smart Garbage Collection System (F-6 Islamabad)

A C++ Data Structures & Algorithms (DSA) project with a full-screen interactive Leaflet.js + OpenStreetMap frontend simulating autonomous garbage collection across the **F-6 Islamabad** road network.

---

## Architecture & DSA Highlights

### C++ Backend (`src/`)
- **Graph (Adjacency List)** — `Graph.h` / `Graph.cpp`: Loads `data/graph.json` (768 nodes, 1,725 directed road edges with exact road polyline geometry, distance in meters, and travel time in seconds).
- **Manual MinHeap / Priority Queue** — `MinHeap.h` / `MinHeap.cpp`: Custom binary min-heap used by Dijkstra's algorithm (no `std::priority_queue`).
- **Dijkstra's Algorithm** — `Graph::dijkstra`: Computes shortest-distance and fastest-route paths along valid road geometry for truck dispatch and disposal station routing.
- **Greedy Algorithm** — `GreedySelection.h` / `GreedySelection.cpp`: Selects the nearest eligible `Available` truck after verifying remaining capacity, battery reserve, and road-network reachability.
- **Manual Hash Table** — `HashTable.h` / `HashTable.cpp`: Separate-chaining hash table for O(1) average lookup of trucks and garbage requests by ID.
- **Truck & Request Simulation** — `Truck.h` / `Truck.cpp`, `GarbageRequest.h` / `GarbageRequest.cpp`: Manages 6 independent trucks roaming connected F-6 roads (avoiding immediate backtracking), collection, capacity splitting/safety, battery consumption, and disposal.
- **HTTP JSON API** — `src/main.cpp` using `include/httplib.h` (`cpp-httplib`) and `include/json.hpp` (`nlohmann/json`).

### Frontend (`frontend/`)
- Full-screen **Leaflet.js** + **OpenStreetMap** interface (`index.html`, `style.css`, `script.js`).
- **Yellow** markers: Moving garbage trucks.
- **Brown** markers: Uncollected garbage requests (snapped to nearest valid F-6 road node).
- **Green** markers: Cleaned garbage locations.
- **Purple** marker (`D`): Designated **F-6 Disposal Station** at connected graph **Node 29** (`33.7360682, 73.0773545`, 4 incoming / 4 outgoing edges).

---

## Project Structure

```text
Garbage/
├── data/
│   ├── graph.json
│   └── road_names.json
├── frontend/
│   ├── index.html
│   ├── style.css
│   └── script.js
├── include/
│   ├── httplib.h
│   └── json.hpp
├── src/
│   ├── main.cpp
│   ├── Graph.h / Graph.cpp
│   ├── MinHeap.h / MinHeap.cpp
│   ├── HashTable.h / HashTable.cpp
│   ├── Truck.h / Truck.cpp
│   ├── GarbageRequest.h / GarbageRequest.cpp
│   └── GreedySelection.h / GreedySelection.cpp
├── build/
├── CMakeLists.txt
├── Makefile
└── README.md
```

---

## Build & Run

### Using `g++` (Windows / MSYS2)
```powershell
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -Isrc src/main.cpp src/MinHeap.cpp src/Graph.cpp src/HashTable.cpp src/Truck.cpp src/GarbageRequest.cpp src/GreedySelection.cpp -o build/SmartGarbage.exe -lws2_32 -pthread
.\build\SmartGarbage.exe
```

### Using `CMake`
```powershell
cmake -B build
cmake --build build --config Release
.\build\SmartGarbage.exe
```

Then open **http://localhost:8080** in your browser and click anywhere on the F-6 map to report garbage.
