#ifndef GARBAGEREQUEST_H
#define GARBAGEREQUEST_H

#include <string>

enum class RequestStatus {
    Pending,
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
    double totalQuantity;
    double remainingQuantity;
    long long cleanedAtUnixMs;
    RequestStatus status;
    std::string assignedTruckId;
    std::string statusNote;

    GarbageRequest();
    GarbageRequest(int reqId, int snappedNodeId, double nodeLat, double nodeLon,
                   double qty);
};

#endif
