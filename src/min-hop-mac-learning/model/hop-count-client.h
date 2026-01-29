// hop-count-client.h

#ifndef HOP_COUNT_CLIENT_H //  <-- 修改
#define HOP_COUNT_CLIENT_H //  <-- 修改

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/packet-socket-address.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"

namespace ns3
{

class Socket;

/**
 * @brief A PacketSocket-based traffic generator that adds a HopCountHeader.
 *
 * This application sends packets of a specified size at a specified interval
 * to a remote address. Each packet includes a HopCountHeader initialized to 0.
 */
class HopCountClient : public Application // <-- 修改类名
{
  public:
    static TypeId GetTypeId();
    HopCountClient(); // <-- 修改
    ~HopCountClient() override; // <-- 修改

    void SetRemote(PacketSocketAddress addr);
    void SetPriority(uint8_t priority);
    void SetisFlood(bool isFlood);
    uint8_t GetPriority() const;

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;
    void Send();

    uint32_t m_size;         //!< Size of the packets
    uint32_t m_sent;         //!< Counter for sent packets
    uint32_t m_maxPackets;   //!< Max number of packets to send
    Time m_interval;         //!< Interval between packets
    EventId m_sendEvent;     //!< Event id of pending "send" event
    Ptr<Socket> m_socket;    //!< The transmission socket
    PacketSocketAddress m_peerAddress; //!< The destination address
    bool m_peerAddressSet;   //!< Flag to check if peer address is set
    uint8_t m_priority;      //!< Priority of the packets
    bool m_isFlood;

    /// Trace sinks
    TracedCallback<Ptr<const Packet>, const Address&> m_txTrace;
};

} // namespace ns3

#endif /* HOP_COUNT_CLIENT_H */