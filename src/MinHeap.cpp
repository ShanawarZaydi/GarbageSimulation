#include "MinHeap.h"
#include <stdexcept>

using namespace std;

void MinHeap::heapifyUp(int index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (heap[index].cost < heap[parent].cost) {
            HeapNode temp = heap[index];
            heap[index] = heap[parent];
            heap[parent] = temp;
            index = parent;
        } else {
            break;
        }
    }
}

void MinHeap::heapifyDown(int index) {
    int n = static_cast<int>(heap.size());
    while (true) {
        int left = 2 * index + 1;
        int right = 2 * index + 2;
        int smallest = index;

        if (left < n && heap[left].cost < heap[smallest].cost) {
            smallest = left;
        }
        if (right < n && heap[right].cost < heap[smallest].cost) {
            smallest = right;
        }

        if (smallest != index) {
            HeapNode temp = heap[index];
            heap[index] = heap[smallest];
            heap[smallest] = temp;
            index = smallest;
        } else {
            break;
        }
    }
}

void MinHeap::push(double cost, int nodeId) {
    HeapNode node;
    node.cost = cost;
    node.nodeId = nodeId;
    heap.push_back(node);
    heapifyUp(static_cast<int>(heap.size()) - 1);
}

HeapNode MinHeap::pop() {
    if (heap.empty()) {
        throw runtime_error("MinHeap is empty");
    }
    HeapNode root = heap[0];
    heap[0] = heap.back();
    heap.pop_back();
    if (!heap.empty()) {
        heapifyDown(0);
    }
    return root;
}

HeapNode MinHeap::top() const {
    if (heap.empty()) {
        throw runtime_error("MinHeap is empty");
    }
    return heap[0];
}

bool MinHeap::empty() const {
    return heap.empty();
}
