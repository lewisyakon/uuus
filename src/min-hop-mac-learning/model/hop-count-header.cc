// hop-count-header.cc
#include "hop-count-header.h"

#include "ns3/buffer.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("HopCountHeader");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(HopCountHeader);

TypeId
HopCountHeader::GetTypeId()
{
  static TypeId tid = TypeId("ns3::HopCountHeader")
    .SetParent<Header>()
    .SetGroupName("Network")
    .AddConstructor<HopCountHeader>();
  return tid;
}

HopCountHeader::HopCountHeader()
  : m_hopCount(0),
    m_isFlood(false)
{
  NS_LOG_FUNCTION(this);
}

HopCountHeader::~HopCountHeader()
{
  NS_LOG_FUNCTION(this);
}

TypeId
HopCountHeader::GetInstanceTypeId() const
{
  return GetTypeId();
}

void
HopCountHeader::Serialize(Buffer::Iterator start) const
{
  // Serialize the hop count (1 byte)
  start.WriteU8(m_hopCount);
  start.WriteU8(m_isFlood ? 1 : 0); 
}

uint32_t
HopCountHeader::Deserialize(Buffer::Iterator start)
{
  // Deserialize the hop count (1 byte)
  m_hopCount = start.ReadU8();
  m_isFlood = ( start.ReadU8() != 0 ); 
  return GetSerializedSize(); // Return the number of bytes deserialized
}

uint32_t
HopCountHeader::GetSerializedSize() const
{
  return 2;
}

void
HopCountHeader::Print(std::ostream& os) const
{
  os << "HopCount: " << static_cast<uint32_t>(m_hopCount)
     << ", IsFlood: " << (m_isFlood ? "true" : "false");
}

void
HopCountHeader::SetHopCount(uint8_t count)
{
  m_hopCount = count;
}

uint8_t
HopCountHeader::GetHopCount() const
{
  return m_hopCount;
}

void
HopCountHeader::SetIsFlood(bool isFlood)
{
  m_isFlood = isFlood;
}

bool
HopCountHeader::IsFlood() const
{
  return m_isFlood;
}

}// namespace ns3      