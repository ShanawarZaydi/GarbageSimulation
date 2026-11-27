#define _WIN32_WINNT 0x0A00
#include "httplib.h"
#include "json.hpp"

#include "GarbageRequest.h"
#include "Graph.h"
#include "GreedySelection.h"
#include "HashTable.h"
#include "MinHeap.h"
#include "Truck.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using json = nlohmann::json;

namespace {
    const int DISPOSAL_NODE_ID = 29;
    const int CHARGING_NODE_IDS[2] = {7, 126};
    const int CHARGING_STATION_COUNT = 2;
    const double TRUCK_SPEED_MPS = 32.0;
    const double BATTERY_PER_METER = 0.008;
    const double CHARGE_RATE_PERCENT_PER_SECOND = 5.0;
    const double CHARGE_DISPATCH_BATTERY_PERCENT = 50.0;
    const double CHARGE_DISPATCH_RADIUS_METERS = 1500.0;
    const double MAX_ROAD_MAINTENANCE_SNAP_METERS = 80.0;
    const double DEFAULT_GARBAGE_QTY = 35.0;
}

struct RoadMaintenance {
    int fromNode;
    int toNode;
};

struct SimulationState {
    struct ChargingStation {
        int nodeId;
        int bayCapacity;
        int occupiedBays;
        queue<int> waitingTruckIndices;
    };

    Graph graph;
    vector<Truck> trucks;
    vector<GarbageRequest> requests;
    vector<ChargingStation> chargingStations;
    vector<RoadMaintenance> roadMaintenance;
    vector<vector<unsigned int>> roamingEdgeVisits;
    HashTable requestIndexById;
    RouteAlgorithm routingAlgorithm = RouteAlgorithm::Dijkstra;
    int nextRequestId = 1;
    string lastDispatchMessage = "System ready. Click anywhere on the F-6 map to report garbage.";
    mutex mtx;
};

    bool samePhysicalRoad(const RoadMaintenance& road, int fromNode, int toNode) {
        return (road.fromNode == fromNode && road.toNode == toNode) ||
               (road.fromNode == toNode && road.toNode == fromNode);
    }

int findChargingStationIndex(const SimulationState& sim, int stationNodeId) {
    for (int i = 0; i < static_cast<int>(sim.chargingStations.size()); i++) {
        if (sim.chargingStations[i].nodeId == stationNodeId) {
            return i;
        }
    }
    return -1;
}

void initializeChargingStations(SimulationState& sim) {
    sim.chargingStations.clear();
    for (int i = 0; i < CHARGING_STATION_COUNT; i++) {
        sim.chargingStations.push_back({CHARGING_NODE_IDS[i], 1, 0, queue<int>()});
    }
}

void releaseChargingBay(SimulationState& sim, int stationNodeId) {
    int stationIndex = findChargingStationIndex(sim, stationNodeId);
    if (stationIndex < 0) {
        return;
    }

    SimulationState::ChargingStation& station = sim.chargingStations[stationIndex];
    if (station.occupiedBays > 0) {
        station.occupiedBays--;
    }

    while (!station.waitingTruckIndices.empty() &&
           station.occupiedBays < station.bayCapacity) {
        int truckIndex = station.waitingTruckIndices.front();
        station.waitingTruckIndices.pop();

        if (truckIndex < 0 || truckIndex >= static_cast<int>(sim.trucks.size())) {
            continue;
        }

        Truck& nextTruck = sim.trucks[truckIndex];
        if (nextTruck.status != TruckStatus::WaitingToCharge ||
            nextTruck.chargingStationNodeId != station.nodeId) {
            continue;
        }

        station.occupiedBays++;
        nextTruck.status = TruckStatus::Charging;
        nextTruck.assignmentReason = "Charging bay available; charging";
        sim.lastDispatchMessage = nextTruck.id +
            " entered the charging bay at Station " + to_string(station.nodeId) + ".";
    }
}

void arriveAtChargingStation(SimulationState& sim, int truckIndex) {
    Truck& truck = sim.trucks[truckIndex];
    int stationIndex = findChargingStationIndex(sim, truck.chargingStationNodeId);
    if (stationIndex < 0) {
        truck.status = TruckStatus::LowBattery;
        truck.clearRoute();
        truck.chargingStationNodeId = -1;
        truck.assignmentReason = "Charging station unavailable";
        return;
    }

    SimulationState::ChargingStation& station = sim.chargingStations[stationIndex];
    truck.clearRoute();
    if (station.occupiedBays < station.bayCapacity) {
        station.occupiedBays++;
        truck.status = TruckStatus::Charging;
        truck.assignmentReason = "Charging at Station " + to_string(station.nodeId);
        sim.lastDispatchMessage = truck.id +
            " entered the charging bay at Station " + to_string(station.nodeId) + ".";
    } else {
        station.waitingTruckIndices.push(truckIndex);
        truck.status = TruckStatus::WaitingToCharge;
        truck.assignmentReason = "Waiting in FIFO queue at Station " + to_string(station.nodeId);
        sim.lastDispatchMessage = truck.id +
            " joined the FIFO charging queue at Station " + to_string(station.nodeId) + ".";
    }
}

vector<int> getChargingStationNodeIds();

int pickNextRoamingEdge(const Graph& graph, const SimulationState& sim,
                        int currentNode, int previousNode) {
    const auto& neighbors = graph.getNeighbors(currentNode);
    if (neighbors.empty()) {
        return -1;
    }

    const vector<int> chargingStationNodeIds = getChargingStationNodeIds();
    vector<bool> canReachChargingStation(neighbors.size(), false);
    bool hasNonBacktrackingChoice = false;
    for (int i = 0; i < static_cast<int>(neighbors.size()); i++) {
        if (!neighbors[i].available) {
            continue;
        }
        canReachChargingStation[i] = graph.nearestRoute(
            neighbors[i].to, chargingStationNodeIds, sim.routingAlgorithm
        ).reachable;
        if (canReachChargingStation[i] && neighbors[i].to != previousNode) {
            hasNonBacktrackingChoice = true;
        }
    }

    MinHeap candidates;
    for (int i = 0; i < static_cast<int>(neighbors.size()); i++) {
        if (!neighbors[i].available || !canReachChargingStation[i]) {
            continue;
        }
        if (hasNonBacktrackingChoice && neighbors[i].to == previousNode) {
            continue;
        }
        candidates.push(static_cast<double>(sim.roamingEdgeVisits[currentNode][i]), i);
    }

    if (candidates.empty()) {
        return -1;
    }

    double leastVisitedCount = candidates.top().cost;
    vector<int> leastVisitedEdges;
    while (!candidates.empty() && candidates.top().cost == leastVisitedCount) {
        leastVisitedEdges.push_back(candidates.pop().nodeId);
    }
    return leastVisitedEdges[rand() % static_cast<int>(leastVisitedEdges.size())];
}

void initializeRoamingEdgeVisits(SimulationState& sim) {
    sim.roamingEdgeVisits.resize(sim.graph.getNodeCount());
    for (int nodeId = 0; nodeId < sim.graph.getNodeCount(); nodeId++) {
        if (sim.graph.isValidNode(nodeId)) {
            sim.roamingEdgeVisits[nodeId].assign(
                sim.graph.getNeighbors(nodeId).size(), 0
            );
        }
    }
}

vector<int> getChargingStationNodeIds() {
    return vector<int>(CHARGING_NODE_IDS, CHARGING_NODE_IDS + CHARGING_STATION_COUNT);
}

string routeAlgorithmName(RouteAlgorithm algorithm) {
    return algorithm == RouteAlgorithm::AStar ? "astar" : "dijkstra";
}

RouteResult routeBetween(const SimulationState& sim, int startNode, int endNode,
                         bool byTime = false) {
    return sim.graph.findRoute(startNode, endNode, sim.routingAlgorithm, byTime);
}

vector<pair<double, double>> routeGeometryFromTruckPosition(
    const Truck& truck, const RouteResult& route
) {
    vector<pair<double, double>> geometry = {{truck.lat, truck.lon}};
    for (const auto& point : route.geometry) {
        if (Graph::haversineMeters(
                geometry.back().first, geometry.back().second,
                point.first, point.second) > 0.1) {
            geometry.push_back(point);
        }
    }
    return geometry;
}

RouteResult nearestChargingRoute(const SimulationState& sim, int startNode) {
    return sim.graph.nearestRoute(
        startNode, getChargingStationNodeIds(), sim.routingAlgorithm
    );
}

double batteryNeededToReachNearestStation(const SimulationState& sim, int startNode) {
    RouteResult route = nearestChargingRoute(sim, startNode);
    if (!route.reachable) {
        return numeric_limits<double>::infinity();
    }
    return route.totalDistance * BATTERY_PER_METER + Truck::BATTERY_RESERVE;
}

double batteryNeededAfterCollection(const SimulationState& sim, const Truck& truck,
                                    const GarbageRequest& req) {
    return GreedySelection::calculatePostCollectionBatteryRequirement(
        sim.graph, truck, req, DISPOSAL_NODE_ID,
        getChargingStationNodeIds(), sim.routingAlgorithm, BATTERY_PER_METER
    );
}

bool hasNearbyEligibleRequest(const SimulationState& sim, const Truck& chargingTruck) {
    Truck candidateTruck = chargingTruck;
    candidateTruck.status = TruckStatus::Available;
    vector<Truck> candidates = {candidateTruck};

    for (const GarbageRequest& req : sim.requests) {
        if (req.status != RequestStatus::Pending) {
            continue;
        }

        RouteResult route = routeBetween(sim, chargingTruck.currentNode, req.nodeId);
        if (!route.reachable || route.totalDistance > CHARGE_DISPATCH_RADIUS_METERS) {
            continue;
        }

        DispatchDecision decision = GreedySelection::selectNearestEligibleTruck(
            sim.graph, candidates, req, DISPOSAL_NODE_ID,
            getChargingStationNodeIds(), sim.routingAlgorithm, BATTERY_PER_METER
        );
        if (decision.assigned) {
            return true;
        }
    }

    return false;
}

bool shouldSendToCharge(const SimulationState& sim, const Truck& truck) {
    int stationRouteStart = truck.getEffectiveStartNode();
    double requiredBattery = batteryNeededToReachNearestStation(sim, stationRouteStart);
    if (truck.hasRoute()) {
        requiredBattery += truck.getRemainingRouteDistanceMeters() * BATTERY_PER_METER;
    }
    return truck.battery <= requiredBattery;
}

void returnAssignedRequestToQueue(SimulationState& sim, Truck& truck) {
    if (truck.assignedRequestId >= 0) {
        int reqIdx = -1;
        if (sim.requestIndexById.find("REQ-" + to_string(truck.assignedRequestId), reqIdx)) {
            GarbageRequest& req = sim.requests[reqIdx];
            if (req.status != RequestStatus::Cleaned) {
                req.status = RequestStatus::Pending;
                req.assignedTruckId = "";
                req.statusNote = "Requeued because " + truck.id + " is diverting to charge";
            }
        }
    }
    truck.assignedRequestId = -1;
}

void tryDispatchPendingRequests(SimulationState& sim) {
    for (int i = 0; i < static_cast<int>(sim.requests.size()); i++) {
        GarbageRequest& req = sim.requests[i];
        if (req.status != RequestStatus::Pending) {
            continue;
        }

        DispatchDecision decision = GreedySelection::selectNearestEligibleTruck(
            sim.graph, sim.trucks, req, DISPOSAL_NODE_ID,
            getChargingStationNodeIds(), sim.routingAlgorithm, BATTERY_PER_METER
        );

        if (decision.assigned) {
            Truck& truck = sim.trucks[decision.truckIndex];
            const TruckStatus previousStatus = truck.status;
            const bool divertedFromCharging =
                previousStatus == TruckStatus::GoingToCharge ||
                previousStatus == TruckStatus::Charging;
            if (previousStatus == TruckStatus::Charging) {
                releaseChargingBay(sim, truck.chargingStationNodeId);
            }
            truck.status = TruckStatus::Travelling;
            truck.assignedRequestId = req.id;
            truck.assignmentReason = decision.reason;
            if (divertedFromCharging) {
                truck.chargingStationNodeId = -1;
                truck.assignRoute(
                    decision.route.nodePath,
                    routeGeometryFromTruckPosition(truck, decision.route),
                    false
                );
            } else {
                truck.assignRouteSmoothly(decision.route.nodePath, decision.route.geometry);
            }

            req.status = RequestStatus::Travelling;
            req.assignedTruckId = truck.id;
            req.statusNote = decision.reason;

            sim.lastDispatchMessage = req.requestCode + " -> " + decision.reason;
        } else {
            req.statusNote = decision.reason;
        }
    }
}

void sendTruckToCharge(SimulationState& sim, Truck& truck) {
    int startNode = truck.getEffectiveStartNode();
    RouteResult bestRoute = nearestChargingRoute(sim, startNode);

    if (bestRoute.reachable) {
        int bestStationId = bestRoute.nodePath.back();
        truck.chargingStationNodeId = bestStationId;
        truck.status = TruckStatus::GoingToCharge;
        truck.assignRouteSmoothly(bestRoute.nodePath, bestRoute.geometry);
        truck.assignmentReason = "Battery reserve reached -> heading to Charging Station (Node " + to_string(bestStationId) + ")";
        sim.lastDispatchMessage = truck.id + " battery safety limit reached (" +
            to_string(static_cast<int>(truck.battery)) + "%), routing to nearest Charging Station (Node " +
            to_string(bestStationId) + ").";
    } else {
        truck.status = TruckStatus::LowBattery;
        truck.chargingStationNodeId = -1;
        truck.clearRoute();
        truck.assignmentReason = "Battery critically low (charging stations unreachable)";
    }
}

bool routeUsesRoad(const Truck& truck, int fromNode, int toNode) {
    for (size_t i = 1; i < truck.routeNodes.size(); i++) {
        int routeFrom = truck.routeNodes[i - 1];
        int routeTo = truck.routeNodes[i];
        if ((routeFrom == fromNode && routeTo == toNode) ||
            (routeFrom == toNode && routeTo == fromNode)) {
            return true;
        }
    }
    return false;
}

int blockedTruckDestination(SimulationState& sim, Truck& truck) {
    switch (truck.roadBlockedFromStatus) {
        case TruckStatus::Travelling: {
            int requestIndex = -1;
            if (sim.requestIndexById.find(
                    "REQ-" + to_string(truck.assignedRequestId), requestIndex)) {
                return sim.requests[requestIndex].nodeId;
            }
            return -1;
        }
        case TruckStatus::Returning:
            return DISPOSAL_NODE_ID;
        case TruckStatus::GoingToCharge:
            return truck.chargingStationNodeId;
        default:
            return -1;
    }
}

bool rerouteTruckAfterRoadClosure(
    SimulationState& sim, Truck& truck, int closedFrom, int closedTo
) {
    if (!truck.hasRoute() || !routeUsesRoad(truck, closedFrom, closedTo)) {
        return false;
    }

    const bool roaming = truck.isRoaming;
    const TruckStatus previousStatus = truck.status;
    const int destination = roaming
        ? (truck.routeNodes.empty() ? -1 : truck.routeNodes.back())
        : (previousStatus == TruckStatus::Travelling
            ? ([&]() {
                int requestIndex = -1;
                return sim.requestIndexById.find(
                    "REQ-" + to_string(truck.assignedRequestId), requestIndex)
                    ? sim.requests[requestIndex].nodeId
                    : -1;
            })()
            : previousStatus == TruckStatus::Returning
                ? DISPOSAL_NODE_ID
                : previousStatus == TruckStatus::GoingToCharge
                    ? truck.chargingStationNodeId
                    : -1);
    const int startNode = sim.graph.findNearestNode(truck.lat, truck.lon);
    if (!sim.graph.isValidNode(startNode)) {
        return false;
    }

    truck.currentNode = startNode;
    truck.previousNode = -1;

    if (roaming) {
        RouteResult route{};
        if (destination >= 0) {
            route = routeBetween(sim, startNode, destination);
        }
        if (route.reachable) {
            truck.assignRoute(
                route.nodePath, routeGeometryFromTruckPosition(truck, route), true
            );
        } else {
            const GraphNode& node = sim.graph.getNode(startNode);
            RouteResult toNearestNode;
            toNearestNode.geometry.push_back({node.lat, node.lon});
            truck.assignRoute(
                {startNode}, routeGeometryFromTruckPosition(truck, toNearestNode), true
            );
        }
        truck.status = TruckStatus::Available;
        truck.assignmentReason = "Roaming route updated around road maintenance";
        return true;
    }

    if (destination < 0) {
        return false;
    }
    RouteResult route = routeBetween(sim, startNode, destination);
    if (!route.reachable && previousStatus == TruckStatus::GoingToCharge) {
        route = nearestChargingRoute(sim, startNode);
        if (route.reachable) {
            truck.chargingStationNodeId = route.nodePath.back();
        }
    }
    if (route.reachable) {
        truck.assignRoute(
            route.nodePath, routeGeometryFromTruckPosition(truck, route), false
        );
        return true;
    }

    truck.roadBlockedFromStatus = previousStatus;
    truck.status = TruckStatus::RoadBlocked;
    truck.clearRoute();
    truck.assignmentReason = "Stopped: no open route around road maintenance";
    return true;
}

int resumeRoadBlockedTrucks(SimulationState& sim) {
    int resumedCount = 0;
    for (Truck& truck : sim.trucks) {
        if (truck.status != TruckStatus::RoadBlocked) {
            continue;
        }

        const TruckStatus previousStatus = truck.roadBlockedFromStatus;
        const int destination = blockedTruckDestination(sim, truck);
        if (destination < 0) {
            continue;
        }

        RouteResult route = routeBetween(sim, truck.currentNode, destination);
        if (!route.reachable && previousStatus == TruckStatus::GoingToCharge) {
            route = nearestChargingRoute(sim, truck.currentNode);
            if (route.reachable) {
                truck.chargingStationNodeId = route.nodePath.back();
            }
        }
        if (!route.reachable) {
            continue;
        }

        truck.status = previousStatus;
        truck.roadBlockedFromStatus = TruckStatus::Available;
        truck.assignRoute(
            route.nodePath, routeGeometryFromTruckPosition(truck, route), false
        );
        truck.assignmentReason = "Route reopened after road maintenance";
        resumedCount++;
    }
    return resumedCount;
}

void stepSimulation(SimulationState& sim, double dtSeconds) {
    lock_guard<mutex> lock(sim.mtx);

    tryDispatchPendingRequests(sim);
    bool releasedTruckFromCharging = false;

    for (int i = 0; i < static_cast<int>(sim.trucks.size()); i++) {
        Truck& truck = sim.trucks[i];

        if (truck.status == TruckStatus::LowBattery ||
            truck.status == TruckStatus::RoadBlocked) {
            continue;
        }

        if (truck.status == TruckStatus::Available) {
            if (truck.battery < CHARGE_DISPATCH_BATTERY_PERCENT ||
                shouldSendToCharge(sim, truck)) {
                sendTruckToCharge(sim, truck);
                continue;
            }

            if (!truck.hasRoute()) {
                int edgeIdx = pickNextRoamingEdge(
                    sim.graph, sim, truck.currentNode, truck.previousNode
                );
                if (edgeIdx >= 0) {
                    const GraphEdge& edge = sim.graph.getNeighbors(truck.currentNode)[edgeIdx];
                    RouteResult fromNextNode = nearestChargingRoute(sim, edge.to);
                    double edgeDistance = 0.0;
                    for (int point = 0; point + 1 < static_cast<int>(edge.geometry.size()); point++) {
                        edgeDistance += Graph::haversineMeters(
                            edge.geometry[point].first, edge.geometry[point].second,
                            edge.geometry[point + 1].first, edge.geometry[point + 1].second
                        );
                    }
                    if (!fromNextNode.reachable) {
                        continue;
                    }
                    double batteryToContinue =
                        (edgeDistance + fromNextNode.totalDistance) * BATTERY_PER_METER +
                        Truck::BATTERY_RESERVE;
                    if (truck.battery <= batteryToContinue) {
                        sendTruckToCharge(sim, truck);
                        continue;
                    }
                    sim.roamingEdgeVisits[truck.currentNode][edgeIdx]++;
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
            int reqIdx = -1;
            if (sim.requestIndexById.find("REQ-" + to_string(truck.assignedRequestId), reqIdx)) {
                const GarbageRequest& req = sim.requests[reqIdx];
                double requiredBattery =
                    truck.getRemainingRouteDistanceMeters() * BATTERY_PER_METER +
                    batteryNeededAfterCollection(sim, truck, req);
                if (truck.battery <= requiredBattery) {
                    returnAssignedRequestToQueue(sim, truck);
                    sendTruckToCharge(sim, truck);
                    continue;
                }
            }

            bool arrived = truck.advanceMovement(dtSeconds, TRUCK_SPEED_MPS, BATTERY_PER_METER, sim.graph);
            if (arrived) {
                truck.status = TruckStatus::Collecting;
                truck.stateTimerSeconds = 1.8;
                truck.clearRoute();

                if (sim.requestIndexById.find("REQ-" + to_string(truck.assignedRequestId), reqIdx)) {
                    sim.requests[reqIdx].status = RequestStatus::Collecting;
                }
            }
        } else if (truck.status == TruckStatus::Collecting) {
            int reqIdx = -1;
            if (sim.requestIndexById.find("REQ-" + to_string(truck.assignedRequestId), reqIdx)) {
                const GarbageRequest& req = sim.requests[reqIdx];
                if (truck.battery <= batteryNeededAfterCollection(sim, truck, req)) {
                    returnAssignedRequestToQueue(sim, truck);
                    sendTruckToCharge(sim, truck);
                    continue;
                }
            }

            truck.stateTimerSeconds -= dtSeconds;
            if (truck.stateTimerSeconds <= 0.0) {
                truck.battery -= Truck::COLLECTION_BATTERY_COST;
                if (truck.battery < 0.0) truck.battery = 0.0;

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
                        req.cleanedAtUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
                        ).count();
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
                    RouteResult toDisposal = routeBetween(sim, truck.currentNode, DISPOSAL_NODE_ID);
                    if (toDisposal.reachable) {
                        truck.status = TruckStatus::Returning;
                        truck.assignRoute(toDisposal.nodePath, toDisposal.geometry, false);
                        truck.assignmentReason = "Capacity full -> returning to Disposal Station";
                        sim.lastDispatchMessage = truck.id + " reached full capacity, returning to Disposal Station.";
                    } else {
                        truck.status = TruckStatus::Available;
                        truck.assignmentReason = "";
                    }
                } else if (shouldSendToCharge(sim, truck)) {
                    sendTruckToCharge(sim, truck);
                } else {
                    truck.status = TruckStatus::Available;
                    truck.assignmentReason = "";
                }
            }
        } else if (truck.status == TruckStatus::Returning) {
            bool arrived = truck.advanceMovement(dtSeconds, TRUCK_SPEED_MPS, BATTERY_PER_METER, sim.graph);
            if (arrived) {
                truck.status = TruckStatus::Disposing;
                truck.stateTimerSeconds = 5.0;
                truck.clearRoute();
            }
        } else if (truck.status == TruckStatus::Disposing) {
            truck.stateTimerSeconds -= dtSeconds;
            if (truck.stateTimerSeconds <= 0.0) {
                truck.currentLoad = 0.0;
                truck.assignmentReason = "";
                if (shouldSendToCharge(sim, truck)) {
                    sendTruckToCharge(sim, truck);
                } else {
                    truck.status = TruckStatus::Available;
                    sim.lastDispatchMessage = truck.id + " emptied at Disposal Station and returned to service.";
                }
            }
        } else if (truck.status == TruckStatus::GoingToCharge) {
            bool arrived = truck.advanceMovement(dtSeconds, TRUCK_SPEED_MPS, BATTERY_PER_METER, sim.graph);
            if (arrived) {
                arriveAtChargingStation(sim, i);
            }
        } else if (truck.status == TruckStatus::WaitingToCharge) {
            continue;
        } else if (truck.status == TruckStatus::Charging) {
            truck.battery = min(
                100.0,
                truck.battery + CHARGE_RATE_PERCENT_PER_SECOND * dtSeconds
            );
            bool fullyCharged = truck.battery >= 100.0;
            bool nearbyRequest =
                truck.battery > CHARGE_DISPATCH_BATTERY_PERCENT &&
                hasNearbyEligibleRequest(sim, truck);
            if (nearbyRequest || fullyCharged) {
                int stationNodeId = truck.chargingStationNodeId;
                releaseChargingBay(sim, stationNodeId);
                truck.chargingStationNodeId = -1;
                truck.status = TruckStatus::Available;
                truck.assignmentReason = "";
                sim.lastDispatchMessage = nearbyRequest
                    ? truck.id + " charged above 50% and is leaving for nearby garbage."
                    : truck.id + " fully charged and is leaving to resume patrol.";
                releasedTruckFromCharging = true;
            }
        }
    }

    if (releasedTruckFromCharging) {
        tryDispatchPendingRequests(sim);
    }
}

void initSimulation(SimulationState& sim) {
    const int startNodes[6] = {5, 12, 14, 19, 30, 40};
    const double startBatteries[6] = {100.0, 94.0, 87.0, 98.0, 81.0, 91.0};

    for (int i = 0; i < 6; i++) {
        int nodeId = startNodes[i];
        if (!sim.graph.isValidNode(nodeId)) {
            nodeId = 0;
        }
        const GraphNode& node = sim.graph.getNode(nodeId);
        string id = "TRK-" + to_string(i + 1);
        Truck t(id, nodeId, node.lat, node.lon, 100.0, startBatteries[i]);
        sim.trucks.push_back(t);

    }
}

int main() {
    srand(static_cast<unsigned int>(time(nullptr)));

    SimulationState sim;
    if (!sim.graph.loadFromJson("data/graph.json")) {
        cerr << "Error loading F-6 graph data from data/graph.json" << endl;
        return 1;
    }

    initializeChargingStations(sim);
    initializeRoamingEdgeVisits(sim);
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
        json chargingJson = json::array();
        for (int i = 0; i < CHARGING_STATION_COUNT; i++) {
            const GraphNode& chg = sim.graph.getNode(CHARGING_NODE_IDS[i]);
            chargingJson.push_back({
                {"nodeId", chg.id},
                {"lat", chg.lat},
                {"lon", chg.lon},
                {"name", "F-6 Charging Station " + to_string(i + 1)},
                {"bayCapacity", sim.chargingStations[i].bayCapacity}
            });
        }
        out["edges"] = edgesJson;
        out["disposalStation"] = {
            {"nodeId", disp.id},
            {"lat", disp.lat},
            {"lon", disp.lon},
            {"name", "F-6 Disposal Station"}
        };
        out["chargingStations"] = chargingJson;
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
                {"cleanedAtUnixMs", r.cleanedAtUnixMs},
                {"status", requestStatusToString(r.status)},
                {"assignedTruckId", r.assignedTruckId},
                {"statusNote", r.statusNote}
            });
        }

        json chargingStationsJson = json::array();
        for (const SimulationState::ChargingStation& station : sim.chargingStations) {
            json waitingQueue = json::array();
            queue<int> waitingTruckIndices = station.waitingTruckIndices;
            while (!waitingTruckIndices.empty()) {
                int truckIndex = waitingTruckIndices.front();
                waitingTruckIndices.pop();
                if (truckIndex >= 0 && truckIndex < static_cast<int>(sim.trucks.size())) {
                    waitingQueue.push_back(sim.trucks[truckIndex].id);
                }
            }
            chargingStationsJson.push_back({
                {"nodeId", station.nodeId},
                {"occupiedBays", station.occupiedBays},
                {"bayCapacity", station.bayCapacity},
                {"waitingQueue", waitingQueue}
            });
        }

        json maintenanceJson = json::array();
        for (const RoadMaintenance& road : sim.roadMaintenance) {
            json points = json::array();
            for (const GraphEdge& edge : sim.graph.getNeighbors(road.fromNode)) {
                if (edge.to == road.toNode) {
                    for (const auto& point : edge.geometry) {
                        points.push_back({point.first, point.second});
                    }
                    break;
                }
            }
            if (points.empty()) {
                const auto& reverseEdges = sim.graph.getNeighbors(road.toNode);
                for (const GraphEdge& edge : reverseEdges) {
                    if (edge.to == road.fromNode) {
                        for (auto point = edge.geometry.rbegin(); point != edge.geometry.rend(); ++point) {
                            points.push_back({point->first, point->second});
                        }
                        break;
                    }
                }
            }
            maintenanceJson.push_back({
                {"from", road.fromNode},
                {"to", road.toNode},
                {"geometry", points}
            });
        }

        const GraphNode& disp = sim.graph.getNode(DISPOSAL_NODE_ID);
        out["trucks"] = trucksJson;
        out["requests"] = requestsJson;
        out["chargingStations"] = chargingStationsJson;
        out["roadMaintenance"] = maintenanceJson;
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
        out["routingAlgorithm"] = routeAlgorithmName(sim.routingAlgorithm);

        res.set_content(out.dump(), "application/json");
    });

    svr.Post("/api/routing", [&](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (const exception&) {
            res.status = 400;
            res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
            return;
        }

        if (!body.contains("algorithm") || !body["algorithm"].is_string()) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing route algorithm\"}", "application/json");
            return;
        }

        string requestedAlgorithm = body["algorithm"].get<string>();
        RouteAlgorithm algorithm;
        if (requestedAlgorithm == "dijkstra") {
            algorithm = RouteAlgorithm::Dijkstra;
        } else if (requestedAlgorithm == "astar") {
            algorithm = RouteAlgorithm::AStar;
        } else {
            res.status = 400;
            res.set_content("{\"error\":\"Algorithm must be dijkstra or astar\"}", "application/json");
            return;
        }

        {
            lock_guard<mutex> lock(sim.mtx);
            sim.routingAlgorithm = algorithm;
        }

        json out = {
            {"algorithm", routeAlgorithmName(algorithm)},
            {"message", "Algorithm updated; active routes will continue unchanged."}
        };
        res.set_content(out.dump(), "application/json");
    });

    svr.Post("/api/maintenance", [&](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (const exception&) {
            res.status = 400;
            res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
            return;
        }

        if (!body.contains("lat") || !body["lat"].is_number() ||
            !body.contains("lon") || !body["lon"].is_number() ||
            !body.contains("closed") || !body["closed"].is_boolean()) {
            res.status = 400;
            res.set_content(
                "{\"error\":\"Expected numeric lat/lon and boolean closed\"}",
                "application/json"
            );
            return;
        }

        double lat = body["lat"].get<double>();
        double lon = body["lon"].get<double>();
        bool closed = body["closed"].get<bool>();
        if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
            res.status = 400;
            res.set_content("{\"error\":\"Coordinates are out of range\"}", "application/json");
            return;
        }

        lock_guard<mutex> lock(sim.mtx);
        RoadMatch match = sim.graph.findNearestRoad(
            lat, lon, MAX_ROAD_MAINTENANCE_SNAP_METERS
        );
        if (!match.found) {
            res.status = 404;
            res.set_content("{\"error\":\"No road found near the selected point\"}", "application/json");
            return;
        }

        auto maintenanceIt = find_if(
            sim.roadMaintenance.begin(), sim.roadMaintenance.end(),
            [&](const RoadMaintenance& road) {
                return samePhysicalRoad(road, match.from, match.to);
            }
        );
        int updatedRoutesCount = 0;

        if (closed) {
            if (!sim.graph.setRoadAvailability(match.from, match.to, false)) {
                res.status = 404;
                res.set_content("{\"error\":\"Selected road is unavailable\"}", "application/json");
                return;
            }
            if (maintenanceIt == sim.roadMaintenance.end()) {
                sim.roadMaintenance.push_back({match.from, match.to});
            }
            for (Truck& truck : sim.trucks) {
                if (rerouteTruckAfterRoadClosure(
                        sim, truck, match.from, match.to)) {
                    updatedRoutesCount++;
                }
            }
            sim.lastDispatchMessage = "Road between nodes " + to_string(match.from) +
                " and " + to_string(match.to) + " marked for maintenance.";
            if (updatedRoutesCount > 0) {
                sim.lastDispatchMessage += " Updated " + to_string(updatedRoutesCount) +
                    " active truck route(s).";
            }
        } else {
            if (maintenanceIt == sim.roadMaintenance.end()) {
                res.status = 404;
                res.set_content("{\"error\":\"Selected road is not under maintenance\"}", "application/json");
                return;
            }
            if (!sim.graph.setRoadAvailability(
                    maintenanceIt->fromNode, maintenanceIt->toNode, true)) {
                res.status = 404;
                res.set_content("{\"error\":\"Selected road could not be reopened\"}", "application/json");
                return;
            }
            sim.lastDispatchMessage = "Road between nodes " +
                to_string(maintenanceIt->fromNode) + " and " +
                to_string(maintenanceIt->toNode) + " reopened.";
            sim.roadMaintenance.erase(maintenanceIt);
        }

        tryDispatchPendingRequests(sim);
        int resumedCount = closed ? 0 : resumeRoadBlockedTrucks(sim);
        updatedRoutesCount += resumedCount;
        if (!closed && resumedCount > 0) {
            sim.lastDispatchMessage += " Resumed " + to_string(resumedCount) +
                " truck route(s).";
        }
        json out = {
            {"from", match.from},
            {"to", match.to},
            {"closed", closed},
            {"message", sim.lastDispatchMessage},
            {"activeRoutesChanged", updatedRoutesCount > 0},
            {"updatedRoutes", updatedRoutesCount},
            {"resumedTrucks", resumedCount}
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
        GarbageRequest newReq(reqId, snappedNode, node.lat, node.lon, qty);

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
