#include "GreedySelection.h"
#include <iomanip>
#include <limits>
#include <sstream>

using namespace std;

DispatchDecision GreedySelection::selectNearestEligibleTruck(
    const Graph& graph,
    const vector<Truck>& trucks,
    const GarbageRequest& req,
    int disposalNodeId,
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

    RouteResult toDisposal = graph.dijkstra(req.nodeId, disposalNodeId, false);
    double disposalCost = toDisposal.reachable ? (toDisposal.totalDistance * batteryPerMeter) : 0.0;

    for (int i = 0; i < static_cast<int>(trucks.size()); i++) {
        const Truck& t = trucks[i];

        if (t.status != TruckStatus::Available) {
            if (t.status == TruckStatus::LowBattery) {
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

        RouteResult route = graph.dijkstra(t.currentNode, req.nodeId, false);
        if (!route.reachable) {
            unreachableCount++;
            continue;
        }

        double tripBatteryNeeded = (route.totalDistance * batteryPerMeter) +
                                   Truck::COLLECTION_BATTERY_COST +
                                   disposalCost +
                                   Truck::BATTERY_RESERVE;

        if (t.battery < tripBatteryNeeded) {
            lowBatteryCount++;
            continue;
        }

        if (route.totalDistance < bestDist) {
            bestDist = route.totalDistance;
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
