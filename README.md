# Smart Garbage Collection System (F-6 Islamabad)

A C++ Data Structures & Algorithms (DSA) project with a full-screen interactive Leaflet.js + OpenStreetMap frontend simulating autonomous garbage collection across the **F-6 Islamabad** road network.

---

## Architecture & DSA Highlights

### C++ Backend (`src/`)
- **Graph (Adjacency List)** — `Graph.h` / `Graph.cpp`: Loads `data/graph.json` (768 nodes, 1,725 directed road edges with exact road polyline geometry, distance in meters, and travel time in seconds).
- **Manual MinHeap / Priority Queue** — `MinHeap.h` / `MinHeap.cpp`: Custom binary min-heap used by Dijkstra's algorithm and to choose among the least-visited roaming roads.
- **Route Finding** — `Graph::dijkstra` and `Graph::aStar`: Choose either algorithm with the map's Route Planner selector. Dijkstra expands by known route cost; A* adds a straight-line distance heuristic to guide the search toward the destination. Both return shortest-distance routes.
- **Greedy Algorithm** — `GreedySelection.h` / `GreedySelection.cpp`: Selects the closest eligible truck after checking capacity and enough battery for the request, disposal if needed, a charging station, and a safety reserve. A truck heading to charge remains eligible when it can safely take a request; it competes on route distance rather than receiving special priority.
- **Manual Hash Table** — `HashTable.h` / `HashTable.cpp`: Separate-chaining hash table for O(1) average lookup of trucks and garbage requests by ID.
- **Truck & Request Simulation** — `Truck.h` / `Truck.cpp`, `GarbageRequest.h` / `GarbageRequest.cpp`: Manages 6 trucks roaming connected F-6 roads, tracking remaining route distance and diverting to charge before roaming or work would leave insufficient battery to reach a station. Charging increases by 5 percentage points per simulated second; trucks leave above 50% for a safe nearby request, or at 100% to resume patrol. Each station has one charging bay and a FIFO queue for waiting trucks. Disposal takes 5 seconds, collected garbage markers remain green for 10 seconds, and the map's top panel shows each truck's battery, load, and activity.
- **Road Maintenance** — Use the map action selector to close or reopen a road segment. Both directions are excluded from Dijkstra/A* and free-roaming choices. Trucks with affected active routes recalculate from their nearest road node; if no detour exists, they stop until a route becomes available.
- **HTTP JSON API** — `src/main.cpp` using `include/httplib.h` (`cpp-httplib`) and `include/json.hpp` (`nlohmann/json`).

### Frontend (`frontend/`)
- Full-screen **Leaflet.js** + **OpenStreetMap** interface (`index.html`, `style.css`, `script.js`).
- **Yellow** markers: Moving garbage trucks.
- **Brown** markers: Uncollected garbage requests (snapped to nearest valid F-6 road node).
- **Green** markers: Cleaned garbage locations.
- **Purple** marker (`D`): Designated **F-6 Disposal Station** at connected graph **Node 29** (`33.7360682, 73.0773545`, 4 incoming / 4 outgoing edges).
- **Blue** markers: Charging stations at graph Nodes **7** and **126**. Trucks monitor the battery needed to reach the nearest charger, retain a reserve, and divert before accepting an unsafe roaming segment or collection trip.

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
.\build\Release\SmartGarbage.exe
```

Then open **http://localhost:8080** in your browser and click anywhere on the F-6 map to report garbage.
