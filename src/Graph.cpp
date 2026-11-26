#include "Graph.h"
#include "MinHeap.h"
#include "json.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace std;
using json = nlohmann::json;

namespace {
    const double PI = 3.14159265358979323846;
    const double EARTH_RADIUS_M = 6371000.0;
}

Graph::Graph() : edgeCount(0) {}

double Graph::haversineMeters(double lat1, double lon1, double lat2, double lon2) {
    double dLat = (lat2 - lat1) * PI / 180.0;
    double dLon = (lon2 - lon1) * PI / 180.0;
    double rLat1 = lat1 * PI / 180.0;
    double rLat2 = lat2 * PI / 180.0;

    double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
               cos(rLat1) * cos(rLat2) * sin(dLon / 2.0) * sin(dLon / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return EARTH_RADIUS_M * c;
}

bool Graph::loadFromJson(const string& filePath) {
    ifstream in(filePath);
    if (!in.is_open()) {
        cerr << "Failed to open graph file: " << filePath << endl;
        return false;
    }

    json data;
    try {
        in >> data;
    } catch (const exception& e) {
        cerr << "JSON parse error: " << e.what() << endl;
        return false;
    }

    if (!data.contains("nodes") || !data.contains("edges")) {
        cerr << "Invalid graph JSON: missing nodes or edges" << endl;
        return false;
    }

    int maxId = -1;
    for (const auto& item : data["nodes"]) {
        int id = item.at("id").get<int>();
        if (id > maxId) {
            maxId = id;
        }
    }

    nodes.assign(maxId + 1, GraphNode{-1, 0, 0.0, 0.0});
    adjList.assign(maxId + 1, vector<GraphEdge>());
    edgeCount = 0;

    for (const auto& item : data["nodes"]) {
        GraphNode node;
        node.id = item.at("id").get<int>();
        node.osmId = item.at("osm_id").get<long long>();
        node.lat = item.at("lat").get<double>();
        node.lon = item.at("lon").get<double>();
        if (node.id >= 0 && node.id <= maxId) {
            nodes[node.id] = node;
        }
    }

    for (const auto& item : data["edges"]) {
        int from = item.at("from").get<int>();
        int to = item.at("to").get<int>();
        if (!isValidNode(from) || !isValidNode(to)) {
            continue;
        }

        GraphEdge edge;
        edge.to = to;
        edge.length = item.at("length").get<double>();
        edge.time = item.at("time").get<double>();

        if (item.contains("geometry") && item["geometry"].is_array()) {
            for (const auto& pt : item["geometry"]) {
                if (pt.is_array() && pt.size() >= 2) {
                    edge.geometry.push_back({pt[0].get<double>(), pt[1].get<double>()});
                }
            }
        }

        if (edge.geometry.empty()) {
            edge.geometry.push_back({nodes[from].lat, nodes[from].lon});
            edge.geometry.push_back({nodes[to].lat, nodes[to].lon});
        }

        adjList[from].push_back(edge);
        edgeCount++;
    }

    return true;
}

int Graph::getNodeCount() const {
    return static_cast<int>(nodes.size());
}

int Graph::getEdgeCount() const {
    return edgeCount;
}

bool Graph::isValidNode(int nodeId) const {
    return nodeId >= 0 && nodeId < static_cast<int>(nodes.size()) && nodes[nodeId].id == nodeId;
}

const GraphNode& Graph::getNode(int nodeId) const {
    if (!isValidNode(nodeId)) {
        throw out_of_range("Invalid node ID");
    }
    return nodes[nodeId];
}

const vector<GraphEdge>& Graph::getNeighbors(int nodeId) const {
    if (!isValidNode(nodeId)) {
        throw out_of_range("Invalid node ID");
    }
    return adjList[nodeId];
}

RouteResult Graph::dijkstra(int startId, int endId, bool byTime) const {
    RouteResult result;
    result.reachable = false;
    result.totalDistance = 0.0;
    result.totalTime = 0.0;

    if (!isValidNode(startId) || !isValidNode(endId)) {
        return result;
    }

    if (startId == endId) {
        result.reachable = true;
        result.nodePath.push_back(startId);
        result.geometry.push_back({nodes[startId].lat, nodes[startId].lon});
        return result;
    }

    int n = static_cast<int>(nodes.size());
    const double INF = numeric_limits<double>::infinity();

    vector<double> dist(n, INF);
    vector<double> otherMetric(n, 0.0);
    vector<int> parent(n, -1);
    vector<int> parentEdgeIdx(n, -1);
    vector<bool> visited(n, false);

    MinHeap pq;
    dist[startId] = 0.0;
    pq.push(0.0, startId);

    while (!pq.empty()) {
        HeapNode current = pq.pop();
        int u = current.nodeId;

        if (visited[u]) {
            continue;
        }
        visited[u] = true;

        if (u == endId) {
            break;
        }

        const vector<GraphEdge>& edges = adjList[u];
        for (int i = 0; i < static_cast<int>(edges.size()); i++) {
            int v = edges[i].to;
            if (visited[v]) {
                continue;
            }

            double weight = byTime ? edges[i].time : edges[i].length;
            double altMetric = byTime ? edges[i].length : edges[i].time;

            if (dist[u] + weight < dist[v]) {
                dist[v] = dist[u] + weight;
                otherMetric[v] = otherMetric[u] + altMetric;
                parent[v] = u;
                parentEdgeIdx[v] = i;
                pq.push(dist[v], v);
            }
        }
    }

    if (dist[endId] == INF) {
        return result;
    }

    vector<int> reversedPath;
    int curr = endId;
    while (curr != -1) {
        reversedPath.push_back(curr);
        curr = parent[curr];
    }

    for (int i = static_cast<int>(reversedPath.size()) - 1; i >= 0; i--) {
        result.nodePath.push_back(reversedPath[i]);
    }

    for (int i = 0; i + 1 < static_cast<int>(result.nodePath.size()); i++) {
        int u = result.nodePath[i];
        int v = result.nodePath[i + 1];
        int eIdx = parentEdgeIdx[v];
        if (eIdx >= 0 && eIdx < static_cast<int>(adjList[u].size())) {
            const auto& geom = adjList[u][eIdx].geometry;
            for (int j = 0; j < static_cast<int>(geom.size()); j++) {
                if (!result.geometry.empty() && j == 0) {
                    continue;
                }
                result.geometry.push_back(geom[j]);
            }
        }
    }

    result.reachable = true;
    result.totalDistance = byTime ? otherMetric[endId] : dist[endId];
    result.totalTime = byTime ? dist[endId] : otherMetric[endId];
    return result;
}

int Graph::findNearestNode(double lat, double lon) const {
    int bestId = -1;
    double bestDist = numeric_limits<double>::infinity();

    for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
        if (nodes[i].id == -1) {
            continue;
        }
        double d = haversineMeters(lat, lon, nodes[i].lat, nodes[i].lon);
        if (d < bestDist) {
            bestDist = d;
            bestId = nodes[i].id;
        }
    }
    return bestId;
}
