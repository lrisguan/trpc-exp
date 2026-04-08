// gateway/consistent_hash.h
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace http::gateway {

// consistent hashing class
class ConsistentHash {
 public:
  using HashFunc = std::function<uint64_t(const std::string&)>;

  //  constructor: using fnv1a hash function by default
  explicit ConsistentHash(HashFunc hash_func = nullptr) 
      : hash_func_(hash_func ? hash_func : DefaultHash) {}

  // add node, weight affects number of virtual nodes
  void AddNode(const std::string& node, int weight = 100);

  // remove node
  void RemoveNode(const std::string& node);

  // locate node by key
  std::string GetNode(const std::string& key);

  // default FNV-1a hash function
  static uint64_t DefaultHash(const std::string& key);

 private:
  // generate virtual nodes for a given node
  void GenerateVirtualNodes(const std::string& node, int weight);

  HashFunc hash_func_;
  std::map<uint64_t, std::string> ring_;  // hash ring
  std::map<std::string, std::set<uint64_t>> node_vnodes_;  // locate virtual nodes
};

}  // namespace http::gateway
