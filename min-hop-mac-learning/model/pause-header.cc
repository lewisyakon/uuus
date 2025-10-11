#include "pause-header.h"
#include "ns3/log.h"
#include "ns3/mac48-address.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PauseHeader");
NS_OBJECT_ENSURE_REGISTERED(PauseHeader);


TypeId
PauseHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::PauseHeader")
                            .SetParent<Header>()
                            .SetGroupName("PointToPoint")
                            .AddConstructor<PauseHeader>();
    return tid;
}

TypeId
PauseHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

PauseHeader::PauseHeader()
    : m_frameType(PAUSE) // 默认为PAUSE
{
}

PauseHeader::PauseHeader(FrameType type, Mac48Address addr)
    : m_frameType(type),
      m_dst(addr)
{
}

void
PauseHeader::SetFrameType(FrameType type)
{
    m_frameType = type;
}

PauseHeader::FrameType
PauseHeader::GetFrameType() const
{
    return static_cast<FrameType>(m_frameType);
}

void
PauseHeader::SetDstAddr(Mac48Address addr)
{
    m_dst = addr;
}

Mac48Address
PauseHeader::GetDstAddr() const
{
    return m_dst;
}

void
PauseHeader::Print(std::ostream& os) const
{
    os << "PauseHeader (Type: " << (m_frameType == PAUSE ? "PAUSE" : "RESUME") 
       << ", Dst: " << m_dst << ")";
}

uint32_t
PauseHeader::GetSerializedSize() const
{
    // 1字节魔数 + 1字节帧类型 + 6字节MAC地址
    return 1 + 1 + 6;
}

void
PauseHeader::Serialize(Buffer::Iterator start) const
{
    // 依次写入魔数、帧类型和目标地址
    start.WriteU8(MAGIC_VALUE);
    start.WriteU8(m_frameType);
    uint8_t buffer[6];
    m_dst.CopyTo(buffer);
    start.Write(buffer, 6);
}

uint32_t
PauseHeader::Deserialize(Buffer::Iterator start)
{
    // 检查是否有足够的字节进行解析
    if (start.GetRemainingSize() < GetSerializedSize())
    {
        // 字节不够，解析失败
        return 0;
    }

    // 1. 读取魔数
    uint8_t magic = start.ReadU8();

    // 2. !! 关键验证 !!
    // 检查读取到的魔数是否和我们定义的一致
    if (magic != MAGIC_VALUE)
    {
        // 魔数不匹配，说明这不是一个Pause帧，解析失败！
        return 0;
    }

    // 3. 魔数验证通过，继续读取剩下的字段
    m_frameType = start.ReadU8();
    uint8_t buffer[6];
    start.Read(buffer, 6);
    m_dst.CopyFrom(buffer);
    
    // 4. 返回成功读取的总字节数
    return GetSerializedSize();
}

} // namespace ns3