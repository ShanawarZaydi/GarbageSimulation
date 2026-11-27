#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <string>
#include <vector>

struct HashEntry {
    std::string key;
    int value;
};

class HashTable {
private:
    std::vector<std::vector<HashEntry>> buckets;
    int bucketCount;

    int hashFunction(const std::string& key) const;

public:
    HashTable(int size = 103);

    void insert(const std::string& key, int value);
    bool find(const std::string& key, int& outValue) const;
};

#endif
