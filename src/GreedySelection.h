#ifndef GREEDYSELECTION_H
#define GREEDYSELECTION_H

#include "GarbageRequest.h"
#include "Graph.h"
#include "Truck.h"
#include <string>
#include <vector>

struct DispatchDecision {
    bool assigned;
    int truckIndex;
    std::string truckId;
    std::string reason;
    RouteResult route;
};

class GreedySelection {
public:
    static double calculatePostCollectionBatteryRequirement(
        const Graph& graph,
        const Truck& truck,
        const GarbageRequest& req,
        int disposalNodeId,
        const std::vector<int>& chargingStationNodeIds,
        RouteAlgorithm algorithm,
        double batteryPerMeter
    );

    static DispatchDecision selectNearestEligibleTruck(
        const Graph& graph,
        const std::vector<Truck>& trucks,
        const GarbageRequest& req,
        int disposalNodeId,
        const std::vector<int>& chargingStationNodeIds,
        RouteAlgorithm algorithm,
        double batteryPerMeter
    );
};

#endif
