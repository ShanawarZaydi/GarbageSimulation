#include "HashTable.h"

using namespace std;

HashTable::HashTable(int size) : bucketCount(size) {
    if (bucketCount < 7) {
        bucketCount = 7;
    }
    buckets.resize(bucketCount);
}

int HashTable::hashFunction(const string& key) const {
    unsigned long hash = 5381;
    for (int i = 0; i < static_cast<int>(key.size()); i++) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(key[i]);
    }
    return static_cast<int>(hash % static_cast<unsigned long>(bucketCount));
}

void HashTable::insert(const string& key, int value) {
    int idx = hashFunction(key);
    for (int i = 0; i < static_cast<int>(buckets[idx].size()); i++) {
        if (buckets[idx][i].key == key) {
            buckets[idx][i].value = value;
            return;
        }
    }
    HashEntry entry;
    entry.key = key;
    entry.value = value;
    buckets[idx].push_back(entry);
}

bool HashTable::find(const string& key, int& outValue) const {
    int idx = hashFunction(key);
    for (int i = 0; i < static_cast<int>(buckets[idx].size()); i++) {
        if (buckets[idx][i].key == key) {
            outValue = buckets[idx][i].value;
            return true;
        }
    }
    return false;
}

bool HashTable::contains(const string& key) const {
    int dummy = 0;
    return find(key, dummy);
}

bool HashTable::remove(const string& key) {
    int idx = hashFunction(key);
    for (int i = 0; i < static_cast<int>(buckets[idx].size()); i++) {
        if (buckets[idx][i].key == key) {
            buckets[idx][i] = buckets[idx].back();
            buckets[idx].pop_back();
            return true;
        }
    }
    return false;
}

void HashTable::clear() {
    for (int i = 0; i < bucketCount; i++) {
        buckets[i].clear();
    }
}
