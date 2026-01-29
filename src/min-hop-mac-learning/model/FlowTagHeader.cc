// 3Pheader.cc
#include "FlowTagHeader.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("FlowTagHeader");
NS_OBJECT_ENSURE_REGISTERED(FlowTagHeader);

TypeId
FlowTagHeader::GetTypeId()
{
  static TypeId tid =
    TypeId("ns3::FlowTagHeader")
      .SetParent<Header>()
      .SetGroupName("Network")
      .AddConstructor<FlowTagHeader>();
  return tid;
}

uint32_t
FlowTagHeader::GetSerializedSize() const
{
  // 固定区：
  // version(1) + nvls(1) + pad(2)
  // + 7 * int32 (channel..receiver, tag_id)
  // + flow_size (8)
  // + vec_len (4)
  // 变长区：tree_flow_list.size() * 4
  // + packetnum(4)
  uint32_t base = 1 + 1 + 2 + 4u*7 + 8 + 4 + 4;
  return base + static_cast<uint32_t>(4 * tree_flow_list.size());
}

void
FlowTagHeader::Serialize(Buffer::Iterator i) const
{
  // 头部：版本 + 标志 + 对齐填充
  i.WriteU8(kVersion);
  i.WriteU8(nvls_on);
  i.WriteU16(0); // padding

  // 固定字段（网络字节序）
  i.WriteHtonU32(static_cast<uint32_t>(channel_id));
  i.WriteHtonU32(static_cast<uint32_t>(chunk_id));
  i.WriteHtonU32(static_cast<uint32_t>(current_flow_id));
  i.WriteHtonU32(static_cast<uint32_t>(child_flow_id));
  i.WriteHtonU32(static_cast<uint32_t>(sender_node));
  i.WriteHtonU32(static_cast<uint32_t>(receiver_node));
  i.WriteHtonU64(static_cast<uint64_t>(flow_size));
  i.WriteHtonU32(static_cast<uint32_t>(tag_id));
  i.WriteHtonU32(static_cast<uint32_t>(packetnum));

  // 变长列表：先写长度，再写每个元素
  i.WriteHtonU32(static_cast<uint32_t>(tree_flow_list.size()));
  for (int32_t v : tree_flow_list) {
    i.WriteHtonU32(static_cast<uint32_t>(v));
  }
}

uint32_t
FlowTagHeader::Deserialize(Buffer::Iterator i)
{
  uint8_t ver = i.ReadU8();
  nvls_on = i.ReadU8();
  (void)i.ReadU16(); // 跳过 padding

  channel_id      = static_cast<int32_t>(i.ReadNtohU32());
  chunk_id        = static_cast<int32_t>(i.ReadNtohU32());
  current_flow_id = static_cast<int32_t>(i.ReadNtohU32());
  child_flow_id   = static_cast<int32_t>(i.ReadNtohU32());
  sender_node     = static_cast<int32_t>(i.ReadNtohU32());
  receiver_node   = static_cast<int32_t>(i.ReadNtohU32());
  flow_size       = i.ReadNtohU64();
  tag_id          = static_cast<int32_t>(i.ReadNtohU32());
  packetnum       = static_cast<uint32_t>(i.ReadNtohU32());

  uint32_t n = i.ReadNtohU32();
  tree_flow_list.clear();
  tree_flow_list.reserve(n);
  for (uint32_t k = 0; k < n; ++k) {
    tree_flow_list.push_back(static_cast<int32_t>(i.ReadNtohU32()));
  }

  // 目前仅定义 ver==1；如需兼容旧版本，可在此分支处理
  (void)ver;
  return GetSerializedSize();
}

void
FlowTagHeader::Print(std::ostream& os) const
{
  os << "ver=" << int(kVersion)
     << " tag=" << tag_id
     << " chan=" << channel_id
     << " chunk=" << chunk_id
     << " cur=" << current_flow_id
     << " child=" << child_flow_id
     << " src=" << sender_node
     << " dst=" << receiver_node
     << " size=" << flow_size
     << " nvls=" << int(nvls_on)
     << " tree=[";
  for (size_t idx = 0; idx < tree_flow_list.size(); ++idx) {
    if (idx) os << ",";
    os << tree_flow_list[idx];
  }
  os << "]";
}

// 需要 ncclFlowTag 的定义；确保上面包含了正确的头文件
void
FlowTagHeader::FromFlowTag(const AstraSim::ncclFlowTag& t)
{
  channel_id      = t.channel_id;
  chunk_id        = t.chunk_id;
  current_flow_id = t.current_flow_id;
  child_flow_id   = t.child_flow_id;
  sender_node     = t.sender_node;
  receiver_node   = t.receiver_node;
  flow_size       = t.flow_size;
  tag_id          = t.tag_id;
  nvls_on         = t.nvls_on ? 1u : 0u;
  // pQps 不序列化/不复制
  tree_flow_list.clear();
  tree_flow_list.reserve(t.tree_flow_list.size());
  for (int v : t.tree_flow_list) tree_flow_list.push_back(static_cast<int32_t>(v));
}

void
FlowTagHeader::ToFlowTag(AstraSim::ncclFlowTag& t) const
{
  t.channel_id      = channel_id;
  t.chunk_id        = chunk_id;
  t.current_flow_id = current_flow_id;
  t.child_flow_id   = child_flow_id;
  t.sender_node     = sender_node;
  t.receiver_node   = receiver_node;
  t.flow_size       = flow_size;
  t.tag_id          = tag_id;
  t.nvls_on         = (nvls_on != 0);
  t.tree_flow_list.clear();
  t.tree_flow_list.reserve(tree_flow_list.size());
  for (int32_t v : tree_flow_list) t.tree_flow_list.push_back(v);
  t.pQps = nullptr; // 由上层通过键（tag/channel/...）重建
}
void
FlowTagHeader::set_packetnum(uint32_t num){
  packetnum=num;
}
uint32_t
FlowTagHeader::get_packetnum(){
  return packetnum;
}

} // namespace ns3