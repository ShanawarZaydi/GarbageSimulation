#include "GreedySelection.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

using namespace std;

double GreedySelection::calculatePostCollectionBatteryRequirement(
    const Graph& graph,
    const Truck& truck,
    const GarbageRequest& req,
    int disposalNodeId,
    const vector<int>& chargingStationNodeIds,
    RouteAlgorithm algorithm,
    double batteryPerMeter
) {
    const double amountCollected = min(req.remainingQuantity, truck.getRemainingCapacity());
    const bool fullAfterCollection =
        truck.getRemainingCapacity() - amountCollected <= 0.01;

    double postWorkDistance = 0.0;
    if (fullAfterCollection) {
        const RouteResult toDisposal =
            graph.findRoute(req.nodeId, disposalNodeId, algorithm, false);
        if (!toDisposal.reachable) {
            return numeric_limits<double>::infinity();
        }

        const RouteResult toCharger =
            graph.nearestRoute(disposalNodeId, chargingStationNodeIds, algorithm);
        if (!toCharger.reachable) {
            return numeric_limits<double>::infinity();
        }
        postWorkDistance = toDisposal.totalDistance + toCharger.totalDistance;
    } else {
        const RouteResult toCharger =
            graph.nearestRoute(req.nodeId, chargingStationNodeIds, algorithm);
        if (!toCharger.reachable) {
            return numeric_limits<double>::infinity();
        }
        postWorkDistance = toCharger.totalDistance;
    }

    return Truck::COLLECTION_BATTERY_COST +
           postWorkDistance * batteryPerMeter +
           Truck::BATTERY_RESERVE;
}

DispatchDecision GreedySelection::selectNearestEligibleTruck(
    const Graph& graph,
    const vector<Truck>& trucks,
    const GarbageRequest& req,
    int disposalNodeId,
    const vector<int>& chargingStationNodeIds,
    RouteAlgorithm algorithm,
    double batteryPerMeter
) {
    DispatchDecision best;
    best.assigned = false;
    best.truckIndex = -1;

    double bestDist = numeric_limits<double>::infinity();
    int busyCount = 0;
    int fullCount = 0;
    int lowBatteryCount = 0;
    int unreachableCount = 0;

    for (int i = 0; i < static_cast<int>(trucks.size()); i++) {
        const Truck& t = trucks[i];
        const bool chargingBound =
            t.status == TruckStatus::GoingToCharge ||
            t.status == TruckStatus::Charging;

        if (t.status != TruckStatus::Available && !chargingBound) {
            if (t.status == TruckStatus::LowBattery ||
                t.status == TruckStatus::RoadBlocked) {
                lowBatteryCount++;
            } else if (t.status == TruckStatus::Returning || t.status == TruckStatus::Disposing) {
                fullCount++;
            } else {
                busyCount++;
            }
            continue;
        }

        if (t.getRemainingCapacity() <= 0.01) {
            fullCount++;
            continue;
        }

        if (t.battery <= Truck::CRITICAL_BATTERY) {
            lowBatteryCount++;
            continue;
        }

        int routeStartNode = chargingBound
            ? graph.findNearestNode(t.lat, t.lon)
            : t.getEffectiveStartNode();
        if (!graph.isValidNode(routeStartNode)) {
            unreachableCount++;
            continue;
        }
        RouteResult route = graph.findRoute(routeStartNode, req.nodeId, algorithm, false);
        if (!route.reachable) {
            unreachableCount++;
            continue;
        }

        const double postCollectionBattery =
            calculatePostCollectionBatteryRequirement(
                graph, t, req, disposalNodeId, chargingStationNodeIds,
                algorithm, batteryPerMeter
            );
        if (!isfinite(postCollectionBattery)) {
            unreachableCount++;
            continue;
        }

        double approachDistance = t.isRoaming
            ? t.getRemainingRouteDistanceMeters()
            : chargingBound
                ? Graph::haversineMeters(
                    t.lat, t.lon,
                    graph.getNode(routeStartNode).lat,
                    graph.getNode(routeStartNode).lon
                )
                : 0.0;
        double tripDistance = approachDistance + route.totalDistance;
        const double tripBatteryNeeded =
            tripDistance * batteryPerMeter + postCollectionBattery;

        if (t.battery <= tripBatteryNeeded) {
            lowBatteryCount++;
            continue;
        }

        if (tripDistance < bestDist) {
            bestDist = tripDistance;
            best.assigned = true;
            best.truckIndex = i;
            best.truckId = t.id;
            best.route = route;
        }
    }

    if (best.assigned) {
        const Truck& chosen = trucks[best.truckIndex];
        ostringstream oss;
        oss << fixed << setprecision(0);
        oss << "Greedy nearest eligible: " << chosen.id
            << " (" << bestDist << " m, "
            << chosen.battery << "% battery, "
            << chosen.getRemainingCapacity() << " kg free)";
        best.reason = oss.str();
    } else {
        ostringstream oss;
        oss << "No eligible truck available (";
        bool first = true;
        if (busyCount > 0) {
            oss << busyCount << " busy";
            first = false;
        }
        if (fullCount > 0) {
            if (!first) oss << ", ";
            oss << fullCount << " full/returning";
            first = false;
        }
        if (lowBatteryCount > 0) {
            if (!first) oss << ", ";
            oss << lowBatteryCount << " low battery";
            first = false;
        }
        if (unreachableCount > 0) {
            if (!first) oss << ", ";
            oss << unreachableCount << " unreachable";
        }
        oss << "). Request queued.";
        best.reason = oss.str();
    }

    return best;
}
