#ifndef MINHEAP_H
#define MINHEAP_H

#include <vector>

struct HeapNode {
    double cost;
    int nodeId;
};

class MinHeap {
private:
    std::vector<HeapNode> heap;

    void heapifyUp(int index);
    void heapifyDown(int index);

public:
    MinHeap() = default;

    void push(double cost, int nodeId);
    HeapNode pop();
    HeapNode top() const;
    bool empty() const;
    int size() const;
    void clear();
};

#endif
