// 3Pheader.h
#ifndef THREE_P_HEADER_H
#define THREE_P_HEADER_H

#include "ns3/header.h"
#include "ns3/type-id.h"

namespace ns3 {

class ThreePHeader : public Header
{
public:
  enum State {
    REQUEST = 0,
    DATA    = 1,
    ACT     = 2,
    IDLE    = 3
  };
  
  ThreePHeader();
  ~ThreePHeader() override;

  void SetState(State state);
  State GetState() const;
  void SetTag(uint32_t tag);
  uint32_t GetTag() const;
  void SetSrc(uint32_t src);
  uint32_t GetSrc() const;
  void SetDst(uint32_t dst);
  uint32_t GetDst() const;
  void SetDataSize(uint64_t dataSize);
  uint64_t GetDataSize() const;


  static TypeId GetTypeId();
  TypeId GetInstanceTypeId() const override;
  void Print(std::ostream &os) const override;
  uint32_t GetSerializedSize() const override;
  void Serialize(Buffer::Iterator start) const override;
  uint32_t Deserialize(Buffer::Iterator start) override;

private:
  uint8_t m_state;
  uint32_t m_tag;
  uint64_t m_datasize;
  uint32_t m_src;
  uint32_t m_dst;
};

} // namespace ns3

#endif /* THREE_P_HEADER_H */