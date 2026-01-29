#ifndef MIN_HOP_BRIDGE_NET_DEVICE_H
#define MIN_HOP_BRIDGE_NET_DEVICE_H

#include <map>
#include <vector>
#include <fstream>
#include "ns3/channel.h"
#include "ns3/mac48-address.h"
#include "ns3/net-device.h"
#include "ns3/traced-callback.h"
#include "ns3/nstime.h"

// 包含自定义的跳数头部
#include "hop-count-header.h"

namespace ns3
{

class BridgeChannel;

/**
 * @ingroup bridge
 * @brief A custom bridge NetDevice that forwards packets based on the minimum hop count
 * and drops packets if hop count exceeds a limit.
 *
 * This device learns which port(s) provide the path with the minimum hop count
 * from a source MAC address. It can forward packets along multiple, equally-short
 * paths. It also implements a TTL-like mechanism by dropping packets when their
 * hop count reaches a threshold (>= 4).
 */
class MinHopBridgeNetDevice : public NetDevice
{
  public:
    static TypeId GetTypeId();
    MinHopBridgeNetDevice();
    ~MinHopBridgeNetDevice() override;

    // These methods are part of the NetDevice API
    void SetIfIndex(const uint32_t index) override;
    uint32_t GetIfIndex() const override;
    Ptr<Channel> GetChannel() const override;
    void SetAddress(Address address) override;
    Address GetAddress() const override;
    bool SetMtu(const uint16_t mtu) override;
    uint16_t GetMtu() const override;
    bool IsLinkUp() const override;
    void AddLinkChangeCallback(Callback<void> callback) override;
    bool IsBroadcast() const override;
    Address GetBroadcast() const override;
    bool IsMulticast() const override;
    Address GetMulticast(Ipv4Address multicastGroup) const override;
    Address GetMulticast(Ipv6Address addr) const override;
    bool IsPointToPoint() const override;
    bool IsBridge() const override;
    bool Send(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber) override;
    bool SendFrom(Ptr<Packet> packet,
                  const Address& src,
                  const Address& dest,
                  uint16_t protocolNumber) override;
    Ptr<Node> GetNode() const override;
    void SetNode(Ptr<Node> node) override;
    bool NeedsArp() const override;
    void SetReceiveCallback(ReceiveCallback cb) override;
    void SetPromiscReceiveCallback(PromiscReceiveCallback cb) override;
    bool SupportsSendFrom() const override;

    // Bridge-specific methods
    void AddBridgePort(Ptr<NetDevice> bridgePort);
    uint32_t GetNBridgePorts() const;
    Ptr<NetDevice> GetBridgePort(uint32_t n) const;
    void PrintLearnedState() const;
    void Print_LocalMacAddress() const;
    
    void AddEntry(Mac48Address mac, const std::vector<uint32_t>& ports);
    void set_ecmp_model(int model);

    void set_maxjump(int jump);
    
    void Set_switchDelay(Time t);

    bool writemactable(std::string filepath);
    bool readmactable(std::string filepath);

    void hostsend(Ptr<NetDevice> incomingPort,Ptr<const Packet> packet,uint16_t protocol,
                    const Address& src,const Address& dst);
    
  protected:
    void DoDispose() override;

  private:
    /**
     * \brief State learned by the bridge for a given MAC address.
     */
    struct LearnedState
    {
        Ptr<NetDevice> associatedPort;
        /** The minimum hop count discovered for this MAC address. */
        uint8_t minHopCount;
        /** The expiration time for this entry. */
        Time expirationTime;

        LearnedState() : minHopCount(255)
        {
        } // Initialize with a large value
    };

    void ScheduleReceive(Ptr<NetDevice> incomingPort,
                           Ptr<const Packet> packet,
                           uint16_t protocol,
                           const Address& src,
                           const Address& dst,
                           PacketType packetType);

    void ReceiveFromDevice(Ptr<NetDevice> incomingPort,
                           Ptr<const Packet> packet,
                           uint16_t protocol,
                           const Address& src,
                           const Address& dst,
                           PacketType packetType);

    void ForwardUnicast(Ptr<NetDevice> incomingPort,
                        Ptr<const Packet> packet,
                        uint16_t protocol,
                        Mac48Address src,
                        Mac48Address dst);

    void ForwardBroadcast(Ptr<NetDevice> incomingPort,
                          Ptr<const Packet> packet,
                          uint16_t protocol,
                          Mac48Address src,
                          Mac48Address dst);

    bool Learn(Mac48Address source, Ptr<NetDevice> port, uint8_t hopCount);
    std::vector<Ptr<NetDevice>> GetLearnedState(Mac48Address source);
    Ptr<NetDevice> ecmp(std::vector<Ptr<NetDevice> > outports, Mac48Address src,  Mac48Address dst, uint64_t uid);

    /** A map of MAC address to learned state. */
    std::map<Mac48Address, std::vector<LearnedState> > m_learnState;

    Ptr<BridgeChannel> m_channel;
    std::vector<Ptr<NetDevice>> m_ports;
    Ptr<Node> m_node;
    uint32_t m_ifIndex;
    Address m_address;
    uint16_t m_mtu;
    ReceiveCallback m_rxCallback;
    PromiscReceiveCallback m_promiscRxCallback;
    Time m_expirationTime;
    bool m_enableLearning;

    double m_highWatermark;
    std::map<Ptr<NetDevice>, std::set<Mac48Address> > port_to_src;
    void handleHighWatermark(Ptr<NetDevice> port, Mac48Address src);
    void handleLowWatermark(Ptr<NetDevice> port);

    int ecmpmodel=0;
    //0 随机发送 
    //1 hash
    //2 best-fit

    int maxjump=4;

    Time m_switchDelay;
    bool is_set_address;
};

} // namespace ns3

#endif /* MIN_HOP_BRIDGE_NET_DEVICE_H */