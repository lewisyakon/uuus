// 3Pheader.h
#ifndef FLOW_TAG_HEADER_H
#define FLOW_TAG_HEADER_H

#include "ns3/header.h"
#include "ns3/type-id.h"
#include "astra-sim/system/MockNcclLog.h"
#include "astra-sim/system/AstraNetworkAPI.hh"

namespace ns3 {

/**
 * FlowTagHeader
 * 把 ncclFlowTag 的关键信息编码进 on-wire 字节（Header），支持变长 tree_flow_list。
 * 注意：pQps 指针不序列化；对端请基于 (tag_id, channel_id, …) 自行查回句柄。
 */
class FlowTagHeader : public Header
{
public:
  static TypeId GetTypeId();
  virtual TypeId GetInstanceTypeId() const override { return GetTypeId(); }

  FlowTagHeader() = default;

  // —— 序列化接口（ns3::Header）——
  virtual uint32_t GetSerializedSize() const override;
  virtual void Serialize(Buffer::Iterator i) const override;
  virtual uint32_t Deserialize(Buffer::Iterator i) override;
  virtual void Print(std::ostream& os) const override;

  // —— 便捷转换：与你的 ncclFlowTag 互相拷贝（可选用）——
  uint32_t get_packetnum();
  void set_packetnum(uint32_t num);
  void FromFlowTag(const AstraSim::ncclFlowTag& t); // 需要在 .cc 中包含 ncclFlowTag 的定义
  void ToFlowTag(AstraSim::ncclFlowTag& t) const;   // 同上

  // —— 字段（使用固定宽度类型，跨平台稳定）——
  int32_t              channel_id      = -1;
  int32_t              chunk_id        = -1;
  int32_t              current_flow_id = -1;
  int32_t              child_flow_id   = -1;
  int32_t              sender_node     = -1;
  int32_t              receiver_node   = -1;
  uint64_t             flow_size       = 0;
  int32_t              tag_id          = -1;
  uint8_t              nvls_on         = 0;              // 0/1
  std::vector<int32_t> tree_flow_list;                   // 变长
  uint32_t             packetnum       = 1; 

  // 版本号常量（当前写入 1）
  static constexpr uint8_t kVersion = 1;
};

} // namespace ns3

#endif /* FLOW_TAG_HEADER_H */