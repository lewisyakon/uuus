#include "min-hop-bridge-net-device.h"
#include "hop-count-header.h"
#include "pause-header.h"

#include "ns3/boolean.h"
#include "ns3/bridge-channel.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/queue.h"
#include "ns3/point-to-point-net-device.h"
#include <algorithm>
#include <sstream>
#include <chrono>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MinHopBridgeNetDevice");
NS_OBJECT_ENSURE_REGISTERED(MinHopBridgeNetDevice);

TypeId
MinHopBridgeNetDevice::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::MinHopBridgeNetDevice")
            .SetParent<NetDevice>()
            .SetGroupName("Bridge")
            .AddConstructor<MinHopBridgeNetDevice>()
            .AddAttribute("Mtu",
                          "The MAC-level Maximum Transmission Unit",
                          UintegerValue(1500),
                          MakeUintegerAccessor(&MinHopBridgeNetDevice::SetMtu,
                                             &MinHopBridgeNetDevice::GetMtu),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("EnableLearning",
                          "Enable the learning mode of the Learning Bridge",
                          BooleanValue(true),
                          MakeBooleanAccessor(&MinHopBridgeNetDevice::m_enableLearning),
                          MakeBooleanChecker())
            .AddAttribute("ExpirationTime",
                          "Time it takes for learned MAC state entry to expire.",
                          TimeValue(Seconds(999999)),
                          MakeTimeAccessor(&MinHopBridgeNetDevice::m_expirationTime),
                          MakeTimeChecker());
    return tid;
}

MinHopBridgeNetDevice::MinHopBridgeNetDevice()
    : m_node(nullptr),
      m_ifIndex(0),
      m_mtu(1500),
      m_expirationTime(Seconds(999999)),
      m_enableLearning(true),
      m_highWatermark(0.8)
{
    NS_LOG_FUNCTION_NOARGS();
    m_channel = CreateObject<BridgeChannel>();
    is_set_address = false;
    // m_switchDelay = Time("10us");
    m_switchDelay = Time("10us");
}

MinHopBridgeNetDevice::~MinHopBridgeNetDevice()
{
    NS_LOG_FUNCTION_NOARGS();
}

void
MinHopBridgeNetDevice::DoDispose()
{
    NS_LOG_FUNCTION_NOARGS();
    for (auto iter = m_ports.begin(); iter != m_ports.end(); iter++)
    {
        *iter = nullptr;
    }
    m_ports.clear();
    m_channel = nullptr;
    m_node = nullptr;
    NetDevice::DoDispose();
}

void MinHopBridgeNetDevice::set_maxjump(int jump){
    maxjump=jump;
}

void
MinHopBridgeNetDevice::ReceiveFromDevice(Ptr<NetDevice> incomingPort,
                                         Ptr<const Packet> packet,
                                         uint16_t protocol,
                                         const Address& src,
                                         const Address& dst,
                                         PacketType packetType)
{
    // NS_LOG_FUNCTION_NOARGS();
    // NS_LOG_DEBUG("UID is " << packet->GetUid());

    Ptr<Packet> p = packet->Copy();
    Simulator::Schedule(m_switchDelay,
                        &MinHopBridgeNetDevice::ScheduleReceive,
                        this,
                        incomingPort,
                        p,
                        protocol,
                        src,
                        dst,
                        packetType);
}

void
MinHopBridgeNetDevice::ScheduleReceive(Ptr<NetDevice> incomingPort,
                                         Ptr<const Packet> packet,
                                         uint16_t protocol,
                                         const Address& src,
                                         const Address& dst,
                                         PacketType packetType)
{
    Ptr<Packet> p = packet->Copy();
    HopCountHeader hopHeader;
    uint8_t currentHops = 0, newHops = 0;

    Mac48Address src48 = Mac48Address::ConvertFrom(src);
    Mac48Address dst48 = Mac48Address::ConvertFrom(dst);

    if (p->RemoveHeader(hopHeader) > 0)
    {
        // NS_LOG_LOGIC("Is Flood: "<<hopHeader.IsFlood());
        if (hopHeader.IsFlood())
        {
            // Packet came from another bridge, use its hop count
            currentHops = hopHeader.GetHopCount();
            newHops = currentHops + 1;

            bool flagmin = Learn(src48, incomingPort, newHops);

            hopHeader.SetHopCount(newHops);      
            if (newHops >= maxjump || flagmin == false)
            {
                // NS_LOG_LOGIC("Packet " << p->GetUid() << " dropped, hop count limit reached ("
                //                     << (uint32_t)newHops << " >= 4)");
                return; // Stop processing this packet
            }            
        }
        else{
            // Time now1=Simulator::Now();
            // NS_LOG_INFO("[SRX] UID=" << packet->GetUid()
            //                << " Received via the switch" 
            //                << " | Time=" << now1.GetFemtoSeconds() << "s");
        }
        p->AddHeader(hopHeader);  
    }

    if (!m_promiscRxCallback.IsNull())
    {
        m_promiscRxCallback(this, p, protocol, src, dst, packetType);
    }

    // Forwarding/Receiving logic
    switch (packetType)
    {
    case PACKET_HOST:
        // Packet is for us. Pass up to local applications.
        if (dst48 == m_address)
        {
            m_rxCallback(this, p, protocol, src);
        }
        break;

    case PACKET_BROADCAST:
    case PACKET_MULTICAST:
        // Pass up to local applications, then flood to other ports.
        m_rxCallback(this, p, protocol, src);
        ForwardBroadcast(incomingPort, p, protocol, src48, dst48);
        break;

    case PACKET_OTHERHOST:
        // Packet is unicast, but not for us. Forward it.
        if (dst48 != m_address)
        {
            if (newHops > 0)
            {
                for (auto iter = m_ports.begin(); iter != m_ports.end(); iter++)
                {
                    Ptr<NetDevice> port = *iter;
                    if (port != incomingPort)
                    {
                        port->SendFrom(p->Copy(), src, dst, protocol);
                    }
                }
            }
            else
            {
                ForwardUnicast(incomingPort, p, protocol, src48, dst48);                
            }
        }
        else // It's for us but was misidentified? Pass up.
        {
            m_rxCallback(this, p, protocol, src);
        }
        break;
    default:
        break;
    }  
}

void 
MinHopBridgeNetDevice::set_ecmp_model(int model){
    if (model>=0 and model<=2){
        ecmpmodel=model;
    }
}

void 
MinHopBridgeNetDevice::AddEntry(Mac48Address mac, const std::vector<uint32_t>& ports){
    std::vector<LearnedState> mstate;
    uint8_t hopCount = 0;
    for (size_t i=0;i<ports.size();i++){
        LearnedState state;
        state.associatedPort = m_ports[ports[i]];
        state.minHopCount = hopCount;
        state.expirationTime = Time::Max(); 
        mstate.push_back(state);
    }
    m_learnState[mac] = mstate;
}

Ptr<NetDevice>
MinHopBridgeNetDevice::ecmp(std::vector<Ptr<NetDevice> > outPorts, Mac48Address src,  Mac48Address dst, uint64_t uid){
    if (ecmpmodel==0){//随机转发
        auto nowsys = std::chrono::system_clock::now();
        auto duration = nowsys.time_since_epoch();
        uint64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        uint32_t seed = static_cast<uint32_t>(now);
        uint32_t index = seed % outPorts.size();
        Ptr<NetDevice> outPort = outPorts[index];

        return outPort;
    }
    //按包hash（src+dst+uid+m_address）/按流hash(流ID？)
    
    if (ecmpmodel==1){//HASH（按源地址和目的地址HASH）
        std::ostringstream oss;
        // 添加源和目的 MAC 地址
        oss << src << dst;
        // 添加本地地址
        oss << m_address;
        // 添加 packet UID
        oss << uid;
        std::string key = oss.str();
        // // 使用标准哈希函数
        std::hash<std::string> hasher;
        size_t hash = hasher(key);

        // 取模得到输出端口索引
        uint32_t index = hash % outPorts.size();
        // NS_LOG_INFO("m_address (converted to MAC): " << m_address);
        return outPorts[index];
    }
    if (ecmpmodel==2){//best-fit
        return nullptr;
    }
    return nullptr;
}

void
MinHopBridgeNetDevice::ForwardUnicast(Ptr<NetDevice> incomingPort,
                                Ptr<const Packet> packet,
                                uint16_t protocol,
                                Mac48Address src,
                                Mac48Address dst)
{
    // NS_LOG_FUNCTION_NOARGS();
    // NS_LOG_DEBUG("LearningBridgeForward (incomingPort="
    //              << incomingPort->GetInstanceTypeId().GetName() << ", packet=" << packet
    //              << ", protocol=" << protocol << ", src=" << src << ", dst=" << dst << ")");

    std::vector<Ptr<NetDevice> > outPort = GetLearnedState(dst);
    // for(auto p:outPort)
    // {
    //     in_to_out[p].insert(src);
    //     // NS_LOG_INFO("Insert src "<< src <<" to map of device"<< Mac48Address::ConvertFrom(p->GetAddress()));
    // }

    if (!outPort.empty())
    {
        Ptr<NetDevice> realoutport=ecmp(outPort,src,dst,packet->GetUid());

        if (realoutport!=incomingPort){

            // Ptr<PointToPointNetDevice> p2prealoutport = DynamicCast<PointToPointNetDevice>(realoutport);
            // Ptr<Queue<Packet> > tx_queue = p2prealoutport->GetQueue();
            // uint32_t current_size = tx_queue -> GetNPackets();
            // current_size = current_size + 1;
            // uint32_t max_size = tx_queue -> GetMaxSize().GetValue();
            // NS_LOG_INFO("At time: "  << Simulator::Now().GetSeconds() << 
            //             "s, current size: " << current_size << " max_size: " << max_size);
            // double ratio = (double)current_size / (double)max_size;
            // if(ratio >= m_highWatermark)
            // {
            //     if(port_to_src[realoutport].count(src) == 0)
            //     {
            //         port_to_src[realoutport].insert(src);
            //         p2prealoutport -> AddPauseCount(1);
            //         // NS_LOG_INFO("At Time:" << Simulator::Now().GetSeconds() <<
            //         //             "s, Port: "<<Mac48Address::ConvertFrom(realoutport->GetAddress()) 
            //         //             << " Insert src: " << src);
            //         handleHighWatermark(realoutport, src);                    
            //     }
            //     // return ;
            // }

            // NS_LOG_LOGIC("Learning bridge state says to use port `"
            //          << realoutport->GetInstanceTypeId().GetName() << "'");
            realoutport->SendFrom(packet->Copy(), src, dst, protocol); 
        }
        
        // for (int i=0;i<outPort.size();i++){
        //     NS_LOG_LOGIC("Learning bridge state says to use port `"
        //              << outPort[i]->GetInstanceTypeId().GetName() << "'");
        //     outPort[i]->SendFrom(packet->Copy(), src, dst, protocol);    
        // }
    }
    else
    {
        // NS_LOG_LOGIC("At time:"<<Simulator::Now().GetSeconds()<< "s");
        // NS_LOG_LOGIC("No learned state: send through all ports");
        for (auto iter = m_ports.begin(); iter != m_ports.end(); iter++)
        {
            Ptr<NetDevice> port = *iter;
            if (port != incomingPort)
            {
                // NS_LOG_LOGIC("LearningBridgeForward ("
                //              << src << " => " << dst
                //              << "): " << incomingPort->GetInstanceTypeId().GetName() << " --> "
                //              << port->GetInstanceTypeId().GetName() << " (UID " << packet->GetUid()
                //              << ").");
                port->SendFrom(packet->Copy(), src, dst, protocol);
            }
        }
    }
}

void
MinHopBridgeNetDevice::ForwardBroadcast(Ptr<NetDevice> incomingPort,
                                        Ptr<const Packet> packet,
                                        uint16_t protocol,
                                        Mac48Address src,
                                        Mac48Address dst)
{
    NS_LOG_FUNCTION_NOARGS();

    // --- FIX: Learning is now done in ReceiveFromDevice, no need to do it here ---

    for (auto iter = m_ports.begin(); iter != m_ports.end(); iter++)
    {
        Ptr<NetDevice> port = *iter;
        if (port != incomingPort)
        {
            // NS_LOG_LOGIC("Flooding (" << src << " => " << dst << "): "
            //                           << " --> port " << port->GetIfIndex()
            //                           << " (UID " << packet->GetUid() << ").");
            port->SendFrom(packet->Copy(), src, dst, protocol);
        }
    }
}

bool
MinHopBridgeNetDevice::Learn(Mac48Address source, Ptr<NetDevice> port, uint8_t hopCount)
{
    // NS_LOG_INFO(this << source << " via port " << port->GetIfIndex() << " with hop count " << (uint32_t)hopCount);
    bool flagmin = true;

    if (!m_enableLearning)
    {
        // NS_LOG_LOGIC("Node " << GetNode()->GetId() << ": Learning is disabled.");
        return false;
    }

    Time now = Simulator::Now();
    std::vector<LearnedState>& states = m_learnState[source]; // Access or create vector for source MAC

    // Find the current overall minimum hop count from existing entries (without considering expiration here).
    uint8_t currentOverallMinHops = 255;
    if (!states.empty())
    {
        for (const auto& ls : states)
        {
            currentOverallMinHops = std::min(currentOverallMinHops, ls.minHopCount);
        }
    }

    // Check if an entry for the incoming port already exists in the current states.
    bool portEntryExists = false;
    for (auto& ls : states)
    {
        if (ls.associatedPort == port)
        {
            portEntryExists = true;
            // If the incoming hop count is better than the currently recorded for this specific port, update it.
            if (hopCount < ls.minHopCount)
            {
                // NS_LOG_LOGIC("Node " << GetNode()->GetId() << ": Updating existing entry for MAC "
                //                      << source << " on port " << port->GetIfIndex()
                //                      << " with better hop count: " << (uint32_t)hopCount
                //                      << " (old: " << (uint32_t)ls.minHopCount << ")");
                ls.minHopCount = hopCount;
            }
            ls.expirationTime = now + m_expirationTime; // Always refresh timer for existing path
            break;
        }
    }

    if (!portEntryExists)
    {
        // This is a new port for this MAC address.
        if (states.empty() || hopCount < currentOverallMinHops)
        {
            // If no entries, or this path is strictly better than any existing path,
            // clear all old entries and add this new best path.
            // NS_LOG_LOGIC("Node " << GetNode()->GetId() << ": Found new best path for MAC "
            //                      << source << " via port " << port->GetIfIndex()
            //                      << " with hop count " << (uint32_t)hopCount
            //                      << ". Clearing older, worse paths.");
            states.clear(); // Clear all previous entries
            LearnedState newLs;
            newLs.associatedPort = port;
            newLs.minHopCount = hopCount;
            newLs.expirationTime = now + m_expirationTime;
            states.push_back(newLs);
        }
        else if (hopCount == currentOverallMinHops)
        {
            // This is an equally good path, add it.
            // NS_LOG_LOGIC("Node " << GetNode()->GetId() << ": Found another equal-cost path for MAC "
            //                      << source << " via port " << port->GetIfIndex()
            //                      << " with hop count " << (uint32_t)hopCount);
            LearnedState newLs;
            newLs.associatedPort = port;
            newLs.minHopCount = hopCount;
            newLs.expirationTime = now + m_expirationTime;
            states.push_back(newLs);
        }
        else
        {
            flagmin = false;
            // This path is worse than existing minimum, ignore it.
            // NS_LOG_LOGIC("Node " << GetNode()->GetId() << ": Ignoring worse path for MAC "
            //                      << source << " via port " << port->GetIfIndex()
            //                      << " with hop count " << (uint32_t)hopCount
            //                      << " (current min: " << (uint32_t)currentOverallMinHops << ")");
        }
    }
    else
    {
        // If an existing port entry was found and potentially updated,
        // we need to re-evaluate if this update changed the overall minimum,
        // and if so, filter out paths that are no longer optimal.
        // This handles cases where an existing path for 'port' becomes the new best.
        uint8_t reEvaluatedMinHops = 255;
        for (const auto& ls : states)
        {
            reEvaluatedMinHops = std::min(reEvaluatedMinHops, ls.minHopCount);
        }

        if (reEvaluatedMinHops < currentOverallMinHops)
        {
            // NS_LOG_LOGIC("Node " << GetNode()->GetId() << ": Re-evaluating learned state for "
            //                      << source << ". New overall min hops: " << (uint32_t)reEvaluatedMinHops
            //                      << " due to update on port " << port->GetIfIndex());
            std::vector<LearnedState> filteredStates;
            for (const auto& ls : states)
            {
                if (ls.minHopCount == reEvaluatedMinHops)
                {
                    filteredStates.push_back(ls);
                }
            }
            states = filteredStates;
        }
    }

    return flagmin;
}

std::vector<Ptr<NetDevice>>
MinHopBridgeNetDevice::GetLearnedState(Mac48Address source)
{
    // NS_LOG_FUNCTION(this << source);
    std::vector<Ptr<NetDevice>> resultPorts;

    if (!m_enableLearning)
    {
        // NS_LOG_LOGIC("Node " << GetNode()->GetId() << ": Learning is disabled, no learned state.");
        return resultPorts;
    }

    Time now = Simulator::Now();
    auto it = m_learnState.find(source);

    if (it != m_learnState.end())
    {
        std::vector<LearnedState>& states = it->second;
        uint8_t overallMinHops = 255;
        std::vector<LearnedState> validStates; // To rebuild the vector without expired entries

        // First pass: Find the overall minimum hop count among valid entries
        for (const auto& ls : states)
        {
            if (ls.expirationTime > now)
            {
                validStates.push_back(ls); // Keep valid entries
                overallMinHops = std::min(overallMinHops, ls.minHopCount);
            }
            else
            {
                // NS_LOG_LOGIC("Node " << GetNode()->GetId() << ": Expired learned entry for MAC "
                //                      << source << " on port " << ls.associatedPort->GetIfIndex()
                //                      << " with hops " << (uint32_t)ls.minHopCount);
            }
        }
        states = validStates; // Update the map's vector, removing expired entries

        // Second pass: Collect all ports that match the overall minimum hop count
        for (const auto& ls : states)
        {
            if (ls.minHopCount == overallMinHops)
            {
                // Ensure no duplicate ports are added if somehow they exist
                bool alreadyAdded = false;
                for (const auto& existingPort : resultPorts)
                {
                    if (existingPort == ls.associatedPort)
                    {
                        alreadyAdded = true;
                        break;
                    }
                }
                if (!alreadyAdded)
                {
                    resultPorts.push_back(ls.associatedPort);
                }
            }
        }
    }
    return resultPorts;
}

uint32_t
MinHopBridgeNetDevice::GetNBridgePorts() const
{
    NS_LOG_FUNCTION_NOARGS();
    return m_ports.size();
}

Ptr<NetDevice>
MinHopBridgeNetDevice::GetBridgePort(uint32_t n) const
{
    NS_LOG_FUNCTION_NOARGS();
    return m_ports[n];
}

void
MinHopBridgeNetDevice::AddBridgePort(Ptr<NetDevice> bridgePort)
{
    NS_LOG_FUNCTION_NOARGS();
    NS_ASSERT(bridgePort != this);
    if (!Mac48Address::IsMatchingType(bridgePort->GetAddress()))
    {
        NS_FATAL_ERROR("Device does not support eui 48 addresses: cannot be added to bridge.");
    }
    if (!bridgePort->SupportsSendFrom())
    {
        NS_FATAL_ERROR("Device does not support SendFrom: cannot be added to bridge.");
    }
    if (m_address == Mac48Address() or is_set_address == false)
    {
        m_address = Mac48Address::ConvertFrom(bridgePort->GetAddress());
        is_set_address = true;
    }
    // bool connected = bridgePort -> TraceConnectWithoutContext("HighWatermark",
    //                             MakeCallback(&MinHopBridgeNetDevice::handleHighWatermark,this).Bind(bridgePort));
    // if(!connected){
    //     NS_LOG_ERROR("fail to connect highwatermark");
    // }     
    
    bool connected = bridgePort -> TraceConnectWithoutContext("LowWatermark",
                                MakeCallback(&MinHopBridgeNetDevice::handleLowWatermark,this).Bind(bridgePort));
    if(!connected){
        NS_LOG_ERROR("fail to connect lowwatermark");
    }                                    

    NS_LOG_DEBUG("RegisterProtocolHandler for " << bridgePort->GetInstanceTypeId().GetName());
    m_node->RegisterProtocolHandler(MakeCallback(&MinHopBridgeNetDevice::ReceiveFromDevice, this),
                                    0,
                                    bridgePort,
                                    true);
    m_ports.push_back(bridgePort);
    m_channel->AddChannel(bridgePort->GetChannel());
}

void
MinHopBridgeNetDevice::Set_switchDelay(Time switchDelay)
{
    m_switchDelay = switchDelay;
}

void
MinHopBridgeNetDevice::SetIfIndex(const uint32_t index)
{
    NS_LOG_FUNCTION_NOARGS();
    m_ifIndex = index;
}

uint32_t
MinHopBridgeNetDevice::GetIfIndex() const
{
    NS_LOG_FUNCTION_NOARGS();
    return m_ifIndex;
}

Ptr<Channel>
MinHopBridgeNetDevice::GetChannel() const
{
    NS_LOG_FUNCTION_NOARGS();
    return m_channel;
}

void
MinHopBridgeNetDevice::SetAddress(Address address)
{
    NS_LOG_FUNCTION_NOARGS();
    m_address = Mac48Address::ConvertFrom(address);
}

Address
MinHopBridgeNetDevice::GetAddress() const
{
    NS_LOG_FUNCTION_NOARGS();
    return m_address;
}

bool
MinHopBridgeNetDevice::SetMtu(const uint16_t mtu)
{
    NS_LOG_FUNCTION_NOARGS();
    m_mtu = mtu;
    return true;
}

uint16_t
MinHopBridgeNetDevice::GetMtu() const
{
    NS_LOG_FUNCTION_NOARGS();
    return m_mtu;
}

bool
MinHopBridgeNetDevice::IsLinkUp() const
{
    NS_LOG_FUNCTION_NOARGS();
    return true;
}

void
MinHopBridgeNetDevice::AddLinkChangeCallback(Callback<void> callback)
{
}

bool
MinHopBridgeNetDevice::IsBroadcast() const
{
    NS_LOG_FUNCTION_NOARGS();
    return true;
}

Address
MinHopBridgeNetDevice::GetBroadcast() const
{
    NS_LOG_FUNCTION_NOARGS();
    return Mac48Address::GetBroadcast();
}

bool
MinHopBridgeNetDevice::IsMulticast() const
{
    NS_LOG_FUNCTION_NOARGS();
    return true;
}

Address
MinHopBridgeNetDevice::GetMulticast(Ipv4Address multicastGroup) const
{
    NS_LOG_FUNCTION(this << multicastGroup);
    Mac48Address multicast = Mac48Address::GetMulticast(multicastGroup);
    return multicast;
}

bool
MinHopBridgeNetDevice::IsPointToPoint() const
{
    NS_LOG_FUNCTION_NOARGS();
    return false;
}

bool
MinHopBridgeNetDevice::IsBridge() const
{
    NS_LOG_FUNCTION_NOARGS();
    return true;
}

bool
MinHopBridgeNetDevice::Send(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
    NS_LOG_FUNCTION_NOARGS();
    return SendFrom(packet, m_address, dest, protocolNumber);
}

bool
MinHopBridgeNetDevice::SendFrom(Ptr<Packet> packet,
                                const Address& src,
                                const Address& dest,
                                uint16_t protocolNumber)
{
    NS_LOG_FUNCTION_NOARGS();
    Mac48Address dst = Mac48Address::ConvertFrom(dest);

    if (!dst.IsGroup())
    {
        std::vector<Ptr<NetDevice>> outPorts = GetLearnedState(dst);
        if (!outPorts.empty())
        {
            for (auto const& outPort : outPorts)
            {
                outPort->SendFrom(packet->Copy(), src, dest, protocolNumber);
            }
            return true;
        }
    }

    for (auto iter = m_ports.begin(); iter != m_ports.end(); iter++)
    {
        Ptr<NetDevice> port = *iter;
        port->SendFrom(packet->Copy(), src, dest, protocolNumber);
    }

    return true;
}

Ptr<Node>
MinHopBridgeNetDevice::GetNode() const
{
    NS_LOG_FUNCTION_NOARGS();
    return m_node;
}

void
MinHopBridgeNetDevice::SetNode(Ptr<Node> node)
{
    NS_LOG_FUNCTION_NOARGS();
    m_node = node;
}

bool
MinHopBridgeNetDevice::NeedsArp() const
{
    NS_LOG_FUNCTION_NOARGS();
    return true;
}

void
MinHopBridgeNetDevice::SetReceiveCallback(NetDevice::ReceiveCallback cb)
{
    NS_LOG_FUNCTION_NOARGS();
    m_rxCallback = cb;
}

void
MinHopBridgeNetDevice::SetPromiscReceiveCallback(NetDevice::PromiscReceiveCallback cb)
{
    NS_LOG_FUNCTION_NOARGS();
    m_promiscRxCallback = cb;
}

bool
MinHopBridgeNetDevice::SupportsSendFrom() const
{
    NS_LOG_FUNCTION_NOARGS();
    return true;
}

Address
MinHopBridgeNetDevice::GetMulticast(Ipv6Address addr) const
{
    NS_LOG_FUNCTION(this << addr);
    return Mac48Address::GetMulticast(addr);
}

void
MinHopBridgeNetDevice::PrintLearnedState() const
{
    NS_LOG_INFO("Learning State for Bridge on Node " << GetNode()->GetId() << " (Device MAC: " << GetAddress() << ")");
    if (m_learnState.empty())
    {
        NS_LOG_INFO("  No learned entries.");
        return;
    }

    Time now = Simulator::Now();
    for (auto const& pair : m_learnState)
    {
        Mac48Address learnedMac = pair.first;
        const std::vector<LearnedState>& states = pair.second; // This is now a vector

        if (states.empty())
        {
            NS_LOG_INFO("  MAC: " << learnedMac << " (No valid entries)");
            continue;
        }

        // Find the overall minimum hop count for this MAC among non-expired entries
        uint8_t currentOverallMinHops = 255;
        bool hasValidEntry = false;
        for (const auto& ls : states)
        {
            if (ls.expirationTime > now)
            {
                currentOverallMinHops = std::min(currentOverallMinHops, ls.minHopCount);
                hasValidEntry = true;
            }
        }

        std::string portsStr = "";
        if (hasValidEntry)
        {
            for (const auto& ls : states)
            {
                std::ostringstream oss;
                oss << Mac48Address::ConvertFrom(ls.associatedPort->GetAddress());
                portsStr += "port " + std::to_string(ls.associatedPort->GetIfIndex()) +
                            " (MAC: " + oss.str() + ", Hops: " + std::to_string((uint32_t)ls.minHopCount) +
                            ", Expires: " + std::to_string((ls.expirationTime - now).GetSeconds()) + "s" +
                            (ls.minHopCount == currentOverallMinHops && ls.expirationTime > now ? " - MIN & VALID" : "") +
                            (ls.expirationTime <= now ? " - EXPIRED" : "") + ") ";
            }
            NS_LOG_INFO("  MAC: " << learnedMac << ", Overall Min Hops (Valid): " << (uint32_t)currentOverallMinHops
                                 << ", Paths: [" << portsStr << "]");
        }
        else
        {
            NS_LOG_INFO("  MAC: " << learnedMac << " (All entries expired or invalid)");
        }
    }
}

void
MinHopBridgeNetDevice::handleHighWatermark(Ptr<NetDevice> port, Mac48Address src)
{
    Mac48Address port_addr = Mac48Address::ConvertFrom(port->GetAddress());
    NS_LOG_INFO("At Time: "<<Simulator::Now().GetSeconds() <<"s, port "<<port_addr<<" send pause to");

    Ptr<Packet> p = Create<Packet>(10);
    PauseHeader header(PauseHeader::PAUSE, src);
    p -> AddHeader(header);

    std::vector<Ptr<NetDevice> > outPort = GetLearnedState(src);
    if (!outPort.empty())
    {
        Ptr<NetDevice> realoutport=ecmp(outPort,port_addr,src,p->GetUid());
        NS_LOG_INFO("mac: "<<src);
        if (realoutport!=port){
            realoutport->SendFrom(p->Copy(), port_addr, src, 0x8808); 
        }
    }
}

void
MinHopBridgeNetDevice::handleLowWatermark(Ptr<NetDevice> port)
{
    Mac48Address port_addr = Mac48Address::ConvertFrom(port->GetAddress());
    uint32_t count = 0;
    std::set<Mac48Address> sourceToresume = port_to_src[port];
    NS_LOG_INFO("At Time: "<<Simulator::Now().GetSeconds()<<"s, port "<<port_addr<<" send resume to");

    for(auto addr:sourceToresume)
    {
        Ptr<Packet> p = Create<Packet>(10);
        PauseHeader header(PauseHeader::RESUME, addr);
        p -> AddHeader(header);

        std::vector<Ptr<NetDevice> > outPort = GetLearnedState(addr);
        if (!outPort.empty())
        {
            Ptr<NetDevice> realoutport=ecmp(outPort,port_addr,addr,p->GetUid());
            NS_LOG_INFO("mac: "<<addr);
            if (realoutport!=port){
                count ++;
                port_to_src[port].erase(addr);
                NS_LOG_INFO("At Time:" << Simulator::Now().GetSeconds() <<
                    "s, Port: "<<Mac48Address::ConvertFrom(port->GetAddress()) 
                    << " erase src: " << addr);
                realoutport->SendFrom(p->Copy(), port_addr, addr, 0x8808); 
            }
        }
    }

    Ptr<PointToPointNetDevice> p2pport = DynamicCast<PointToPointNetDevice>(port);
    p2pport -> SubPauseCount(count);
}

} // namespace ns3