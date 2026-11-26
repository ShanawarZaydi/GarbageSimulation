#include "Truck.h"

using namespace std;

string truckStatusToString(TruckStatus status) {
    switch (status) {
        case TruckStatus::Available: return "Available";
        case TruckStatus::Assigned: return "Assigned";
        case TruckStatus::Travelling: return "Travelling";
        case TruckStatus::Collecting: return "Collecting";
        case TruckStatus::Returning: return "Returning";
        case TruckStatus::Disposing: return "Disposing";
        case TruckStatus::GoingToCharge: return "GoingToCharge";
        case TruckStatus::Charging: return "Charging";
        case TruckStatus::LowBattery: return "LowBattery";
    }
    return "Available";
}

Truck::Truck()
    : currentNode(-1),
      previousNode(-1),
      lat(0.0),
      lon(0.0),
      maxCapacity(100.0),
      currentLoad(0.0),
      battery(100.0),
      status(TruckStatus::Available),
      assignedRequestId(-1),
      geomIndex(0),
      segmentProgressMeters(0.0),
      isRoaming(false),
      stateTimerSeconds(0.0) {}

Truck::Truck(const string& truckId, int startNode, double startLat, double startLon,
             double capacity, double initialBattery)
    : id(truckId),
      currentNode(startNode),
      previousNode(-1),
      lat(startLat),
      lon(startLon),
      maxCapacity(capacity),
      currentLoad(0.0),
      battery(initialBattery),
      status(TruckStatus::Available),
      assignedRequestId(-1),
      geomIndex(0),
      segmentProgressMeters(0.0),
      isRoaming(false),
      stateTimerSeconds(0.0) {}

double Truck::getRemainingCapacity() const {
    double rem = maxCapacity - currentLoad;
    return rem > 0.0 ? rem : 0.0;
}

bool Truck::isFull() const {
    return getRemainingCapacity() <= 0.01;
}

bool Truck::hasRoute() const {
    return routeGeometry.size() >= 2 && geomIndex + 1 < static_cast<int>(routeGeometry.size());
}

int Truck::getEffectiveStartNode() const {
    if (hasRoute() && isRoaming && !routeNodes.empty()) {
        return routeNodes.back();
    }
    return currentNode;
}

void Truck::assignRoute(const vector<int>& nodes,
                        const vector<pair<double, double>>& geometry,
                        bool roaming) {
    routeNodes = nodes;
    routeGeometry = geometry;
    geomIndex = 0;
    segmentProgressMeters = 0.0;
    isRoaming = roaming;
    if (!routeGeometry.empty()) {
        lat = routeGeometry[0].first;
        lon = routeGeometry[0].second;
    }
}

void Truck::assignRouteSmoothly(const vector<int>& dijkstraNodes,
                                const vector<pair<double, double>>& dijkstraGeom) {
    if (!(hasRoute() && isRoaming)) {
        assignRoute(dijkstraNodes, dijkstraGeom, false);
        return;
    }

    vector<int> combinedNodes;
    vector<pair<double, double>> combinedGeom;

    combinedGeom.push_back({lat, lon});
    for (int k = geomIndex + 1; k < static_cast<int>(routeGeometry.size()); k++) {
        combinedGeom.push_back(routeGeometry[k]);
    }

    if (!routeNodes.empty()) {
        combinedNodes.push_back(routeNodes.back());
    }

    for (int k = 1; k < static_cast<int>(dijkstraNodes.size()); k++) {
        combinedNodes.push_back(dijkstraNodes[k]);
    }
    for (int k = 1; k < static_cast<int>(dijkstraGeom.size()); k++) {
        combinedGeom.push_back(dijkstraGeom[k]);
    }

    routeNodes = combinedNodes;
    routeGeometry = combinedGeom;
    geomIndex = 0;
    segmentProgressMeters = 0.0;
    isRoaming = false;
}

void Truck::clearRoute() {
    routeNodes.clear();
    routeGeometry.clear();
    geomIndex = 0;
    segmentProgressMeters = 0.0;
    isRoaming = false;
}

bool Truck::advanceMovement(double dtSeconds, double speedMps, double batteryPerMeter, const Graph& graph) {
    if (routeGeometry.size() < 2) {
        return true;
    }

    double remainingStep = speedMps * dtSeconds;

    while (remainingStep > 0.0 && geomIndex + 1 < static_cast<int>(routeGeometry.size())) {
        double lat1 = routeGeometry[geomIndex].first;
        double lon1 = routeGeometry[geomIndex].second;
        double lat2 = routeGeometry[geomIndex + 1].first;
        double lon2 = routeGeometry[geomIndex + 1].second;

        double segLen = Graph::haversineMeters(lat1, lon1, lat2, lon2);
        if (segLen < 0.01) {
            geomIndex++;
            segmentProgressMeters = 0.0;
            continue;
        }

        double distLeftInSeg = segLen - segmentProgressMeters;
        if (remainingStep >= distLeftInSeg) {
            remainingStep -= distLeftInSeg;
            battery -= distLeftInSeg * batteryPerMeter;
            if (battery < 0.0) battery = 0.0;

            geomIndex++;
            segmentProgressMeters = 0.0;
            lat = lat2;
            lon = lon2;
        } else {
            segmentProgressMeters += remainingStep;
            battery -= remainingStep * batteryPerMeter;
            if (battery < 0.0) battery = 0.0;

            double t = segmentProgressMeters / segLen;
            lat = lat1 + t * (lat2 - lat1);
            lon = lon1 + t * (lon2 - lon1);
            remainingStep = 0.0;
        }
    }

    if (geomIndex + 1 >= static_cast<int>(routeGeometry.size())) {
        if (!routeNodes.empty()) {
            if (routeNodes.size() >= 2) {
                previousNode = routeNodes[routeNodes.size() - 2];
            }
            currentNode = routeNodes.back();
            if (graph.isValidNode(currentNode)) {
                const GraphNode& node = graph.getNode(currentNode);
                lat = node.lat;
                lon = node.lon;
            }
        }
        return true;
    }

    return false;
}
