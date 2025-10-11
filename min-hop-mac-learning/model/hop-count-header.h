// hop-count-header.h
#ifndef HOP_COUNT_HEADER_H
#define HOP_COUNT_HEADER_H

#include "ns3/header.h"

namespace ns3
{

/**
 * @ingroup network
 * @brief A simple header to store a hop count.
 *
 * This header contains a single 8-bit field, m_hopCount, to track
 * the number of hops a packet has traversed.
 */
class HopCountHeader : public Header
{
  public:
    static TypeId GetTypeId();
    HopCountHeader();
    ~HopCountHeader() override;

    // Inherited from Header
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    // Public accessors
    void SetHopCount(uint8_t count);
    void SetIsFlood(bool isFlood);
    uint8_t GetHopCount() const;
    bool IsFlood() const;

  private:
    uint8_t m_hopCount;
    bool m_isFlood; 
};

} // namespace ns3

#endif /* HOP_COUNT_HEADER_H */