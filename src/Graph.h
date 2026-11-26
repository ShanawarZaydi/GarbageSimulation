#ifndef GRAPH_H
#define GRAPH_H

#include <string>
#include <utility>
#include <vector>

struct GraphNode {
    int id;
    long long osmId;
    double lat;
    double lon;
};

struct GraphEdge {
    int to;
    double length;
    double time;
    std::vector<std::pair<double, double>> geometry;
};

struct RouteResult {
    bool reachable;
    double totalDistance;
    double totalTime;
    std::vector<int> nodePath;
    std::vector<std::pair<double, double>> geometry;
};

class Graph {
private:
    std::vector<GraphNode> nodes;
    std::vector<std::vector<GraphEdge>> adjList;
    int edgeCount;

public:
    Graph();

    bool loadFromJson(const std::string& filePath);

    int getNodeCount() const;
    int getEdgeCount() const;
    bool isValidNode(int nodeId) const;
    const GraphNode& getNode(int nodeId) const;
    const std::vector<GraphEdge>& getNeighbors(int nodeId) const;

    RouteResult dijkstra(int startId, int endId, bool byTime = false) const;
    int findNearestNode(double lat, double lon) const;

    static double haversineMeters(double lat1, double lon1, double lat2, double lon2);
};

#endif
