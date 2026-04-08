// gateway/consistent_hash.h
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace http::gateway {

// 一致性哈希环实现
class ConsistentHash {
 public:
  using HashFunc = std::function<uint64_t(const std::string&)>;

  // 构造函数，默认使用fnv1a哈希函数
  explicit ConsistentHash(HashFunc hash_func = nullptr) 
      : hash_func_(hash_func ? hash_func : DefaultHash) {}

  // 添加节点，weight为权重（影响虚拟节点数量）
  void AddNode(const std::string& node, int weight = 100);

  // 移除节点
  void RemoveNode(const std::string& node);

  // 根据key查找对应的节点
  std::string GetNode(const std::string& key);

  // 默认哈希函数（fnv1a）
  static uint64_t DefaultHash(const std::string& key);

 private:
  // 生成虚拟节点
  void GenerateVirtualNodes(const std::string& node, int weight);

  HashFunc hash_func_;
  std::map<uint64_t, std::string> ring_;  // 哈希环
  std::map<std::string, std::set<uint64_t>> node_vnodes_;  // 节点对应的虚拟节点
};

}  // namespace http::gateway
