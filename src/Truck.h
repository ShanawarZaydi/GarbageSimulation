#ifndef TRUCK_H
#define TRUCK_H

#include "Graph.h"
#include <string>
#include <utility>
#include <vector>

enum class TruckStatus {
    Available,
    Assigned,
    Travelling,
    Collecting,
    Returning,
    Disposing,
    GoingToCharge,
    Charging,
    LowBattery
};

std::string truckStatusToString(TruckStatus status);

class Truck {
public:
    std::string id;
    int currentNode;
    int previousNode;
    double lat;
    double lon;

    double maxCapacity;
    double currentLoad;
    double battery;

    TruckStatus status;
    int assignedRequestId;
    std::string assignmentReason;

    std::vector<int> routeNodes;
    std::vector<std::pair<double, double>> routeGeometry;
    int geomIndex;
    double segmentProgressMeters;
    bool isRoaming;
    double stateTimerSeconds;

    Truck();
    Truck(const std::string& truckId, int startNode, double startLat, double startLon,
          double capacity = 100.0, double initialBattery = 100.0);

    double getRemainingCapacity() const;
    bool isFull() const;
    bool hasRoute() const;
    int getEffectiveStartNode() const;

    void assignRoute(const std::vector<int>& nodes,
                     const std::vector<std::pair<double, double>>& geometry,
                     bool roaming);
    void assignRouteSmoothly(const std::vector<int>& dijkstraNodes,
                             const std::vector<std::pair<double, double>>& dijkstraGeom);
    void clearRoute();

    // Advances the truck along routeGeometry; returns true when destination is reached.
    bool advanceMovement(double dtSeconds, double speedMps, double batteryPerMeter, const Graph& graph);

    static constexpr double CRITICAL_BATTERY = 10.0;
    static constexpr double BATTERY_RESERVE = 15.0;
    static constexpr double COLLECTION_BATTERY_COST = 3.0;
};

#endif
