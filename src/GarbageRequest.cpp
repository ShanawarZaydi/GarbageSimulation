#include "GarbageRequest.h"

using namespace std;

string requestStatusToString(RequestStatus status) {
    switch (status) {
        case RequestStatus::Pending: return "Pending";
        case RequestStatus::Travelling: return "Travelling";
        case RequestStatus::Collecting: return "Collecting";
        case RequestStatus::Cleaned: return "Cleaned";
    }
    return "Pending";
}

GarbageRequest::GarbageRequest()
    : id(0),
      nodeId(-1),
      lat(0.0),
      lon(0.0),
      totalQuantity(0.0),
      remainingQuantity(0.0),
      cleanedAtUnixMs(0),
      status(RequestStatus::Pending) {}

GarbageRequest::GarbageRequest(int reqId, int snappedNodeId, double nodeLat, double nodeLon,
                               double qty)
    : id(reqId),
      requestCode("REQ-" + to_string(reqId)),
      nodeId(snappedNodeId),
      lat(nodeLat),
      lon(nodeLon),
      totalQuantity(qty),
      remainingQuantity(qty),
      cleanedAtUnixMs(0),
      status(RequestStatus::Pending),
      assignedTruckId(""),
      statusNote("Pending truck assignment") {}
