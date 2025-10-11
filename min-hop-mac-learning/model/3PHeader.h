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

  static TypeId GetTypeId();
  TypeId GetInstanceTypeId() const override;
  void Print(std::ostream &os) const override;
  uint32_t GetSerializedSize() const override;
  void Serialize(Buffer::Iterator start) const override;
  uint32_t Deserialize(Buffer::Iterator start) override;

private:
  uint8_t m_state;
};

} // namespace ns3

#endif /* THREE_P_HEADER_H */