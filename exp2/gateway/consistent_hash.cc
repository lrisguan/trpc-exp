// gateway/consistent_hash.cc
#include "consistent_hash.h"
#include <cstdint>

namespace http::gateway {

uint64_t ConsistentHash::DefaultHash(const std::string& key) {
  const uint64_t fnv1a_offset = 14695981039346656037ULL;
  const uint64_t fnv1a_prime = 1099511628211ULL;

  uint64_t hash = fnv1a_offset;
  for (char c : key) {
    hash ^= static_cast<uint64_t>(c);
    hash *= fnv1a_prime;
  }
  return hash;
}

void ConsistentHash::AddNode(const std::string& node, int weight) {
  RemoveNode(node);  // remove existing node if present
  GenerateVirtualNodes(node, weight);
}

void ConsistentHash::RemoveNode(const std::string& node) {
  auto it = node_vnodes_.find(node);
  if (it != node_vnodes_.end()) {
    for (uint64_t vhash : it->second) {
      ring_.erase(vhash);
    }
    node_vnodes_.erase(it);
  }
}

std::string ConsistentHash::GetNode(const std::string& key) {
  if (ring_.empty()) {
    return "";
  }

  uint64_t hash = hash_func_(key);
  auto it = ring_.lower_bound(hash);
  
  // if cannot find, return the first node (circular structure)
  if (it == ring_.end()) {
    it = ring_.begin();
  }

  return it->second;
}

void ConsistentHash::GenerateVirtualNodes(const std::string& node, int weight) {
  //  each 50 weight -> 1 vnode  
  int vnode_count = weight / 50;
  if (vnode_count < 1) vnode_count = 1;

  for (int i = 0; i < vnode_count; ++i) {
    std::string vnode = node + "#" + std::to_string(i);
    uint64_t hash = hash_func_(vnode);
    ring_[hash] = node;
    node_vnodes_[node].insert(hash);
  }
}

}  // namespace http::gateway