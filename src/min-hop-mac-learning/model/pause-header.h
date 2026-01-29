#ifndef PAUSE_HEADER_H
#define PAUSE_HEADER_H

#include "ns3/header.h"
#include "ns3/mac48-address.h"

namespace ns3
{

class PauseHeader : public Header
{
  public:
    enum FrameType
    {
        PAUSE,
        RESUME
    };
    // 定义一个独特的魔数作为 Pause 帧的签名
    static const uint8_t MAGIC_VALUE = 0xBA; 

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    PauseHeader();
    PauseHeader(FrameType type, Mac48Address addr);

    void SetFrameType(FrameType type);
    FrameType GetFrameType() const;

    void SetDstAddr(Mac48Address addr);
    Mac48Address GetDstAddr() const;

    void Print(std::ostream& os) const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;

  private:
    uint8_t m_frameType;
    Mac48Address m_dst;
};

} // namespace ns3

#endif /* PAUSE_HEADER_H */