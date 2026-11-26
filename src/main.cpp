#define _WIN32_WINNT 0x0A00
#include "httplib.h"
#include "json.hpp"

#include "GarbageRequest.h"
#include "Graph.h"
#include "GreedySelection.h"
#include "HashTable.h"
#include "Truck.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using json = nlohmann::json;

namespace {
    const int DISPOSAL_NODE_ID = 29;
    const double TRUCK_SPEED_MPS = 32.0;
    const double BATTERY_PER_METER = 0.008;
    const double DEFAULT_GARBAGE_QTY = 35.0;
}

struct SimulationState {
    Graph graph;
    vector<Truck> trucks;
    vector<GarbageRequest> requests;
    HashTable truckIndexById;
    HashTable requestIndexById;
    int nextRequestId = 1;
    string lastDispatchMessage = "System ready. Click anywhere on the F-6 map to report garbage.";
    mutex mtx;
};

int pickNextRoamingNode(const Graph& graph, int currentNode, int previousNode) {
    const auto& neighbors = graph.getNeighbors(currentNode);
    if (neighbors.empty()) {
        return -1;
    }
    if (neighbors.size() == 1) {
        return 0;
    }

    vector<int> candidates;
    for (int i = 0; i < static_cast<int>(neighbors.size()); i++) {
        if (neighbors[i].to != previousNode) {
            candidates.push_back(i);
        }
    }

    if (candidates.empty()) {
        return rand() % static_cast<int>(neighbors.size());
    }
    return candidates[rand() % static_cast<int>(candidates.size())];
}

void tryDispatchPendingRequests(SimulationState& sim) {
    for (int i = 0; i < static_cast<int>(sim.requests.size()); i++) {
        GarbageRequest& req = sim.requests[i];
        if (req.status != RequestStatus::Pending) {
            continue;
        }

        DispatchDecision decision = GreedySelection::selectNearestEligibleTruck(
            sim.graph, sim.trucks, req, DISPOSAL_NODE_ID, BATTERY_PER_METER
        );

        if (decision.assigned) {
            Truck& truck = sim.trucks[decision.truckIndex];
            truck.status = TruckStatus::Travelling;
            truck.assignedRequestId = req.id;
            truck.assignmentReason = decision.reason;
            truck.assignRoute(decision.route.nodePath, decision.route.geometry, false);

            req.status = RequestStatus::Travelling;
            req.assignedTruckId = truck.id;
            req.statusNote = decision.reason;

            sim.lastDispatchMessage = req.requestCode + " -> " + decision.reason;
        } else {
            req.statusNote = decision.reason;
        }
    }
}

void stepSimulation(SimulationState& sim, double dtSeconds) {
    lock_guard<mutex> lock(sim.mtx);

    tryDispatchPendingRequests(sim);

    for (int i = 0; i < static_cast<int>(sim.trucks.size()); i++) {
        Truck& truck = sim.trucks[i];

        if (truck.status == TruckStatus::LowBattery) {
            continue;
        }

        if (truck.status == TruckStatus::Available) {
            if (truck.battery <= Truck::CRITICAL_BATTERY) {
                truck.status = TruckStatus::LowBattery;
                truck.clearRoute();
                truck.assignmentReason = "Battery critically low (" + to_string(static_cast<int>(truck.battery)) + "%)";
                continue;
            }

            if (!truck.hasRoute()) {
                int edgeIdx = pickNextRoamingNode(sim.graph, truck.currentNode, truck.previousNode);
                if (edgeIdx >= 0) {
                    const GraphEdge& edge = sim.graph.getNeighbors(truck.currentNode)[edgeIdx];
                    vector<int> nodes = {truck.currentNode, edge.to};
                    truck.assignRoute(nodes, edge.geometry, true);
                }
            }

            if (truck.hasRoute()) {
                bool arrived = truck.advanceMovement(dtSeconds, TRUCK_SPEED_MPS, BATTERY_PER_METER, sim.graph);
                if (arrived) {
                    truck.clearRoute();
                }
            }
        } else if (truck.status == TruckStatus::Travelling) {
            bool arrived = truck.advanceMovement(dtSeconds, TRUCK_SPEED_MPS, BATTERY_PER_METER, sim.graph);
            if (arrived) {
                truck.status = TruckStatus::Collecting;
                truck.stateTimerSeconds = 1.8;
                truck.clearRoute();

                int reqIdx = -1;
                if (sim.requestIndexById.find("REQ-" + to_string(truck.assignedRequestId), reqIdx)) {
                    sim.requests[reqIdx].status = RequestStatus::Collecting;
                }
            }
        } else if (truck.status == TruckStatus::Collecting) {
            truck.stateTimerSeconds -= dtSeconds;
            if (truck.stateTimerSeconds <= 0.0) {
                truck.battery -= Truck::COLLECTION_BATTERY_COST;
                if (truck.battery < 0.0) truck.battery = 0.0;

                int reqIdx = -1;
                if (sim.requestIndexById.find("REQ-" + to_string(truck.assignedRequestId), reqIdx)) {
                    GarbageRequest& req = sim.requests[reqIdx];
                    double canTake = req.remainingQuantity;
                    if (canTake > truck.getRemainingCapacity()) {
                        canTake = truck.getRemainingCapacity();
                    }

                    truck.currentLoad += canTake;
                    req.remainingQuantity -= canTake;

                    if (req.remainingQuantity <= 0.01) {
                        req.remainingQuantity = 0.0;
                        req.status = RequestStatus::Cleaned;
                        req.statusNote = "Cleaned by " + truck.id;
                    } else {
                        req.status = RequestStatus::Pending;
                        req.assignedTruckId = "";
                        req.statusNote = "Partially collected by " + truck.id +
                                         "; " + to_string(static_cast<int>(req.remainingQuantity)) + " kg remaining";
                    }
                }

                truck.assignedRequestId = -1;

                if (truck.isFull()) {
                    RouteResult toDisposal = sim.graph.dijkstra(truck.currentNode, DISPOSAL_NODE_ID, false);
                    if (toDisposal.reachable) {
                        truck.status = TruckStatus::Returning;
                        truck.assignRoute(toDisposal.nodePath, toDisposal.geometry, false);
                        truck.assignmentReason = "Capacity full -> returning to Disposal Station";
                        sim.lastDispatchMessage = truck.id + " reached full capacity, returning to Disposal Station.";
                    } else {
                        truck.status = TruckStatus::Available;
                        truck.assignmentReason = "";
                    }
                } else if (truck.battery <= Truck::CRITICAL_BATTERY) {
                    truck.status = TruckStatus::LowBattery;
                    truck.assignmentReason = "Battery critically low";
                } else {
                    truck.status = TruckStatus::Available;
                    truck.assignmentReason = "";
                }
            }
        } else if (truck.status == TruckStatus::Returning) {
            bool arrived = truck.advanceMovement(dtSeconds, TRUCK_SPEED_MPS, BATTERY_PER_METER, sim.graph);
            if (arrived) {
                truck.status = TruckStatus::Disposing;
                truck.stateTimerSeconds = 2.0;
                truck.clearRoute();
            }
        } else if (truck.status == TruckStatus::Disposing) {
            truck.stateTimerSeconds -= dtSeconds;
            if (truck.stateTimerSeconds <= 0.0) {
                truck.currentLoad = 0.0;
                truck.assignmentReason = "";
                if (truck.battery <= Truck::CRITICAL_BATTERY) {
                    truck.status = TruckStatus::LowBattery;
                    truck.assignmentReason = "Battery critically low";
                } else {
                    truck.status = TruckStatus::Available;
                    sim.lastDispatchMessage = truck.id + " emptied at Disposal Station and returned to service.";
                }
            }
        }
    }
}

void initSimulation(SimulationState& sim) {
    const int startNodes[6] = {5, 7, 14, 19, 30, 40};
    const double startBatteries[6] = {100.0, 96.0, 92.0, 98.0, 94.0, 90.0};

    for (int i = 0; i < 6; i++) {
        int nodeId = startNodes[i];
        if (!sim.graph.isValidNode(nodeId)) {
            nodeId = 0;
        }
        const GraphNode& node = sim.graph.getNode(nodeId);
        string id = "TRK-" + to_string(i + 1);
        Truck t(id, nodeId, node.lat, node.lon, 100.0, startBatteries[i]);
        sim.trucks.push_back(t);
        sim.truckIndexById.insert(id, i);
    }
}

int main() {
    srand(static_cast<unsigned int>(time(nullptr)));

    SimulationState sim;
    if (!sim.graph.loadFromJson("data/graph.json")) {
        cerr << "Error loading F-6 graph data from data/graph.json" << endl;
        return 1;
    }

    initSimulation(sim);

    cout << "=== Smart Garbage Collection System ===" << endl;
    cout << "Loaded F-6 Road Network: " << sim.graph.getNodeCount() << " nodes, "
         << sim.graph.getEdgeCount() << " edges." << endl;
    cout << "Initialized " << sim.trucks.size() << " trucks. Disposal Station at Node "
         << DISPOSAL_NODE_ID << "." << endl;

    atomic<bool> running(true);
    thread simThread([&]() {
        const double dt = 0.1;
        while (running.load()) {
            this_thread::sleep_for(chrono::milliseconds(100));
            stepSimulation(sim, dt);
        }
    });

    httplib::Server svr;
    svr.set_mount_point("/", "./frontend");

    svr.Get("/api/graph", [&](const httplib::Request&, httplib::Response& res) {
        lock_guard<mutex> lock(sim.mtx);
        json out;

        json edgesJson = json::array();
        for (int u = 0; u < sim.graph.getNodeCount(); u++) {
            if (!sim.graph.isValidNode(u)) continue;
            const auto& neighbors = sim.graph.getNeighbors(u);
            for (int j = 0; j < static_cast<int>(neighbors.size()); j++) {
                json pts = json::array();
                for (int k = 0; k < static_cast<int>(neighbors[j].geometry.size()); k++) {
                    pts.push_back({neighbors[j].geometry[k].first, neighbors[j].geometry[k].second});
                }
                edgesJson.push_back(pts);
            }
        }

        const GraphNode& disp = sim.graph.getNode(DISPOSAL_NODE_ID);
        out["edges"] = edgesJson;
        out["disposalStation"] = {
            {"nodeId", disp.id},
            {"lat", disp.lat},
            {"lon", disp.lon},
            {"name", "F-6 Disposal Station"}
        };
        out["nodeCount"] = sim.graph.getNodeCount();
        out["edgeCount"] = sim.graph.getEdgeCount();

        res.set_content(out.dump(), "application/json");
    });

    svr.Get("/api/state", [&](const httplib::Request&, httplib::Response& res) {
        lock_guard<mutex> lock(sim.mtx);
        json out;

        json trucksJson = json::array();
        int availableCount = 0;
        for (int i = 0; i < static_cast<int>(sim.trucks.size()); i++) {
            const Truck& t = sim.trucks[i];
            if (t.status == TruckStatus::Available) {
                availableCount++;
            }

            json routePts = json::array();
            if (!t.isRoaming && t.routeGeometry.size() >= 2) {
                for (int k = t.geomIndex; k < static_cast<int>(t.routeGeometry.size()); k++) {
                    routePts.push_back({t.routeGeometry[k].first, t.routeGeometry[k].second});
                }
            }

            trucksJson.push_back({
                {"id", t.id},
                {"currentNode", t.currentNode},
                {"lat", t.lat},
                {"lon", t.lon},
                {"maxCapacity", t.maxCapacity},
                {"currentLoad", t.currentLoad},
                {"remainingCapacity", t.getRemainingCapacity()},
                {"battery", t.battery},
                {"status", truckStatusToString(t.status)},
                {"assignedRequestId", t.assignedRequestId},
                {"assignmentReason", t.assignmentReason},
                {"activeRoute", routePts}
            });
        }

        json requestsJson = json::array();
        int pendingCount = 0;
        int cleanedCount = 0;
        for (int i = 0; i < static_cast<int>(sim.requests.size()); i++) {
            const GarbageRequest& r = sim.requests[i];
            if (r.status == RequestStatus::Cleaned) {
                cleanedCount++;
            } else {
                pendingCount++;
            }

            requestsJson.push_back({
                {"id", r.id},
                {"requestCode", r.requestCode},
                {"nodeId", r.nodeId},
                {"lat", r.lat},
                {"lon", r.lon},
                {"totalQuantity", r.totalQuantity},
                {"remainingQuantity", r.remainingQuantity},
                {"status", requestStatusToString(r.status)},
                {"assignedTruckId", r.assignedTruckId},
                {"statusNote", r.statusNote}
            });
        }

        const GraphNode& disp = sim.graph.getNode(DISPOSAL_NODE_ID);
        out["trucks"] = trucksJson;
        out["requests"] = requestsJson;
        out["disposalStation"] = {
            {"nodeId", disp.id},
            {"lat", disp.lat},
            {"lon", disp.lon},
            {"name", "F-6 Disposal Station"}
        };
        out["lastDispatchMessage"] = sim.lastDispatchMessage;
        out["stats"] = {
            {"availableTrucks", availableCount},
            {"totalTrucks", static_cast<int>(sim.trucks.size())},
            {"activeRequests", pendingCount},
            {"cleanedRequests", cleanedCount}
        };

        res.set_content(out.dump(), "application/json");
    });

    svr.Post("/api/request", [&](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
            return;
        }

        if (!body.contains("lat") || !body.contains("lon")) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing lat or lon\"}", "application/json");
            return;
        }

        double clickLat = body.at("lat").get<double>();
        double clickLon = body.at("lon").get<double>();
        double qty = body.contains("quantity") ? body.at("quantity").get<double>() : DEFAULT_GARBAGE_QTY;
        if (qty <= 0.0) qty = DEFAULT_GARBAGE_QTY;

        lock_guard<mutex> lock(sim.mtx);
        int snappedNode = sim.graph.findNearestNode(clickLat, clickLon);
        if (!sim.graph.isValidNode(snappedNode)) {
            res.status = 500;
            res.set_content("{\"error\":\"No valid road node found\"}", "application/json");
            return;
        }

        const GraphNode& node = sim.graph.getNode(snappedNode);
        int reqId = sim.nextRequestId++;
        GarbageRequest newReq(reqId, snappedNode, node.lat, node.lon, clickLat, clickLon, qty);

        int idx = static_cast<int>(sim.requests.size());
        sim.requests.push_back(newReq);
        sim.requestIndexById.insert(newReq.requestCode, idx);

        tryDispatchPendingRequests(sim);

        const GarbageRequest& updated = sim.requests[idx];
        json out = {
            {"id", updated.id},
            {"requestCode", updated.requestCode},
            {"nodeId", updated.nodeId},
            {"lat", updated.lat},
            {"lon", updated.lon},
            {"status", requestStatusToString(updated.status)},
            {"assignedTruckId", updated.assignedTruckId},
            {"statusNote", updated.statusNote}
        };

        res.set_content(out.dump(), "application/json");
    });

    cout << "Server running at http://localhost:8080" << endl;
    svr.listen("0.0.0.0", 8080);

    running.store(false);
    if (simThread.joinable()) {
        simThread.join();
    }
    return 0;
}
