#ifndef GARBAGEREQUEST_H
#define GARBAGEREQUEST_H

#include <string>

enum class RequestStatus {
    Pending,
    Assigned,
    Travelling,
    Collecting,
    Cleaned
};

std::string requestStatusToString(RequestStatus status);

struct GarbageRequest {
    int id;
    std::string requestCode;
    int nodeId;
    double lat;
    double lon;
    double clickedLat;
    double clickedLon;
    double totalQuantity;
    double remainingQuantity;
    RequestStatus status;
    std::string assignedTruckId;
    std::string statusNote;

    GarbageRequest();
    GarbageRequest(int reqId, int snappedNodeId, double nodeLat, double nodeLon,
                   double clickLat, double clickLon, double qty);
};

#endif
