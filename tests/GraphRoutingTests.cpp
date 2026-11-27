#include "Graph.h"
#include "GreedySelection.h"

#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

namespace {
    bool usesRoad(const std::vector<int>& path, int from, int to) {
        for (size_t i = 1; i < path.size(); ++i) {
            if ((path[i - 1] == from && path[i] == to) ||
                (path[i - 1] == to && path[i] == from)) {
                return true;
            }
        }
        return false;
    }

    bool routesMatch(const RouteResult& dijkstra, const RouteResult& aStar, bool byTime) {
        if (dijkstra.reachable != aStar.reachable) {
            return false;
        }
        if (!dijkstra.reachable) {
            return true;
        }

        const double dijkstraCost = byTime ? dijkstra.totalTime : dijkstra.totalDistance;
        const double aStarCost = byTime ? aStar.totalTime : aStar.totalDistance;
        return std::abs(dijkstraCost - aStarCost) <= 0.01 &&
               !dijkstra.nodePath.empty() &&
               !aStar.nodePath.empty() &&
               dijkstra.nodePath.front() == aStar.nodePath.front() &&
               dijkstra.nodePath.back() == aStar.nodePath.back();
    }
}

int main() {
    Graph graph;
    if (!graph.loadFromJson("data/graph.json")) {
        std::cerr << "Failed to load graph fixture\n";
        return 1;
    }

    const std::vector<std::pair<int, int>> routes = {
        {5, 29},
        {19, 126},
        {40, 7},
        {14, 30}
    };

    for (const auto& endpoints : routes) {
        RouteResult dijkstraDistance = graph.dijkstra(endpoints.first, endpoints.second);
        RouteResult aStarDistance = graph.aStar(endpoints.first, endpoints.second);
        if (!dijkstraDistance.reachable ||
            !routesMatch(dijkstraDistance, aStarDistance, false)) {
            std::cerr << "A* distance route disagrees with Dijkstra for "
                      << endpoints.first << " -> " << endpoints.second << '\n';
            return 1;
        }

        RouteResult dijkstraTime = graph.dijkstra(endpoints.first, endpoints.second, true);
        RouteResult aStarTime = graph.aStar(endpoints.first, endpoints.second, true);
        if (!routesMatch(dijkstraTime, aStarTime, true)) {
            std::cerr << "A* time route disagrees with Dijkstra for "
                      << endpoints.first << " -> " << endpoints.second << '\n';
            return 1;
        }
    }

    RouteResult sameNode = graph.aStar(29, 29);
    if (!sameNode.reachable || sameNode.totalDistance != 0.0 ||
        sameNode.nodePath.size() != 1 || sameNode.nodePath.front() != 29) {
        std::cerr << "A* failed for a route whose start equals its destination\n";
        return 1;
    }

    if (graph.aStar(-1, 29).reachable) {
        std::cerr << "A* accepted an invalid start node\n";
        return 1;
    }

    RouteResult originalRoute = graph.dijkstra(5, 29);
    if (!originalRoute.reachable || originalRoute.nodePath.size() < 2) {
        std::cerr << "Failed to get a route for the road closure test\n";
        return 1;
    }
    const int closedFrom = originalRoute.nodePath[0];
    const int closedTo = originalRoute.nodePath[1];
    if (!graph.setRoadAvailability(closedFrom, closedTo, false)) {
        std::cerr << "Failed to close a road segment\n";
        return 1;
    }

    RouteResult detourDijkstra = graph.dijkstra(5, 29);
    RouteResult detourAStar = graph.aStar(5, 29);
    if ((detourDijkstra.reachable && usesRoad(detourDijkstra.nodePath, closedFrom, closedTo)) ||
        (detourAStar.reachable && usesRoad(detourAStar.nodePath, closedFrom, closedTo)) ||
        detourDijkstra.reachable != detourAStar.reachable) {
        std::cerr << "A router used the road segment while it was closed\n";
        return 1;
    }

    if (!graph.setRoadAvailability(closedFrom, closedTo, true)) {
        std::cerr << "Failed to reopen a road segment\n";
        return 1;
    }
    if (!graph.dijkstra(5, 29).reachable || !graph.aStar(5, 29).reachable) {
        std::cerr << "Routers failed to use the reopened road network\n";
        return 1;
    }

    const GraphNode& requestNode = graph.getNode(12);
    GarbageRequest request(
        1, requestNode.id, requestNode.lat, requestNode.lon,
        10.0
    );
    const GraphNode& chargingTruckNode = graph.getNode(5);
    Truck chargingTruck(
        "TRK-CHARGING", chargingTruckNode.id,
        chargingTruckNode.lat, chargingTruckNode.lon, 100.0, 80.0
    );
    chargingTruck.status = TruckStatus::GoingToCharge;
    chargingTruck.chargingStationNodeId = 7;

    const GraphNode& availableTruckNode = graph.getNode(requestNode.id);
    Truck availableTruck(
        "TRK-AVAILABLE", availableTruckNode.id,
        availableTruckNode.lat, availableTruckNode.lon, 100.0, 100.0
    );
    std::vector<Truck> dispatchCandidates = {chargingTruck, availableTruck};
    const std::vector<int> chargingStations = {7, 126};
    DispatchDecision nearestAvailable = GreedySelection::selectNearestEligibleTruck(
        graph, dispatchCandidates, request, 29, chargingStations,
        RouteAlgorithm::Dijkstra, 0.008
    );
    if (!nearestAvailable.assigned || nearestAvailable.truckId != "TRK-AVAILABLE") {
        std::cerr << "Dispatch did not select the closest eligible truck\n";
        return 1;
    }

    const GraphNode& closeRequestNode = graph.getNode(chargingTruckNode.id);
    GarbageRequest closeRequest(
        2, closeRequestNode.id, closeRequestNode.lat, closeRequestNode.lon,
        10.0
    );
    DispatchDecision nearestChargingBound = GreedySelection::selectNearestEligibleTruck(
        graph, dispatchCandidates, closeRequest, 29, chargingStations,
        RouteAlgorithm::Dijkstra, 0.008
    );
    if (!nearestChargingBound.assigned ||
        nearestChargingBound.truckId != "TRK-CHARGING") {
        std::cerr << "A closest eligible truck heading to charge was excluded\n";
        return 1;
    }

    dispatchCandidates[0].status = TruckStatus::Charging;
    DispatchDecision nearestChargingTruck = GreedySelection::selectNearestEligibleTruck(
        graph, dispatchCandidates, closeRequest, 29, chargingStations,
        RouteAlgorithm::Dijkstra, 0.008
    );
    if (!nearestChargingTruck.assigned ||
        nearestChargingTruck.truckId != "TRK-CHARGING") {
        std::cerr << "A closest eligible truck currently charging was excluded\n";
        return 1;
    }

    dispatchCandidates[0].battery = Truck::CRITICAL_BATTERY;
    DispatchDecision criticalBattery = GreedySelection::selectNearestEligibleTruck(
        graph, dispatchCandidates, request, 29, chargingStations,
        RouteAlgorithm::Dijkstra, 0.008
    );
    if (!criticalBattery.assigned || criticalBattery.truckId != "TRK-AVAILABLE") {
        std::cerr << "A truck at critical battery was incorrectly diverted from charging\n";
        return 1;
    }

    RouteResult toRequest = graph.findRoute(
        chargingTruckNode.id, request.nodeId, RouteAlgorithm::Dijkstra, false
    );
    RouteResult fromRequestToCharger =
        graph.nearestRoute(request.nodeId, chargingStations, RouteAlgorithm::Dijkstra);
    const double postCollectionBattery =
        Truck::COLLECTION_BATTERY_COST +
        fromRequestToCharger.totalDistance * 0.008 +
        Truck::BATTERY_RESERVE;
    dispatchCandidates[0].status = TruckStatus::GoingToCharge;
    dispatchCandidates[0].battery =
        toRequest.totalDistance * 0.008 + postCollectionBattery;
    dispatchCandidates[1].battery = Truck::CRITICAL_BATTERY;
    DispatchDecision exactReserve = GreedySelection::selectNearestEligibleTruck(
        graph, dispatchCandidates, request, 29, chargingStations,
        RouteAlgorithm::Dijkstra, 0.008
    );
    if (exactReserve.assigned) {
        std::cerr << "A truck at exactly the post-collection reserve was dispatched\n";
        return 1;
    }

    return 0;
}
