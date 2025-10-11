// 3Pheader.cc
#include "3PHeader.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("ThreePHeader");
NS_OBJECT_ENSURE_REGISTERED(ThreePHeader);

TypeId
ThreePHeader::GetTypeId()
{
  static TypeId tid = TypeId("ns3::ThreePHeader")
    .SetParent<Header>()
    .SetGroupName("Network")
    .AddConstructor<ThreePHeader>();
  return tid;
}

TypeId
ThreePHeader::GetInstanceTypeId() const
{
  return GetTypeId();
}

ThreePHeader::ThreePHeader()
  : m_state(IDLE)
{
}

ThreePHeader::~ThreePHeader()
{
}

void
ThreePHeader::SetState(State state)
{
  m_state = static_cast<uint8_t>(state);
}

ThreePHeader::State
ThreePHeader::GetState() const
{
  return static_cast<State>(m_state);
}

uint32_t
ThreePHeader::GetSerializedSize() const
{
  return 1; // 只有 1 字节状态
}

void
ThreePHeader::Serialize(Buffer::Iterator start) const
{
  start.WriteU8(m_state);
}

uint32_t
ThreePHeader::Deserialize(Buffer::Iterator start)
{
  m_state = start.ReadU8();
  return 1;
}

void
ThreePHeader::Print(std::ostream &os) const
{
  os << "state=" << (uint32_t)m_state;
}

} // namespace ns3