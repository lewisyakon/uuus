// hop-count-client.cc

#include "hop-count-client.h" // <-- 包含我们自己的头文件
#include "hop-count-header.h" // <-- 包含 HopCountHeader

#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/socket.h"
#include "ns3/uinteger.h"
#include "ns3/packet-socket-factory.h"
#include "ns3/ppp-header.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("HopCountClient"); // <-- 修改日志组件名称

NS_OBJECT_ENSURE_REGISTERED(HopCountClient); // <-- 修改类名

TypeId
HopCountClient::GetTypeId() // <-- 修改类名
{
    // 注意：这里的 TypeId 字符串必须是唯一的
    static TypeId tid =
        TypeId("ns3::HopCountClient") // <-- 修改 TypeId 字符串
            .SetParent<Application>()
            .SetGroupName("Network")
            .AddConstructor<HopCountClient>() // <-- 修改类名
            .AddAttribute("MaxPackets",
                          "The maximum number of packets the application will send (zero means infinite)",
                          UintegerValue(100),
                          MakeUintegerAccessor(&HopCountClient::m_maxPackets), // <-- 修改类名
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Interval",
                          "The time to wait between packets",
                          TimeValue(Seconds(1)),
                          MakeTimeAccessor(&HopCountClient::m_interval), // <-- 修改类名
                          MakeTimeChecker())
            .AddAttribute("PacketSize",
                          "Size of packets generated (bytes).",
                          UintegerValue(1024),
                          MakeUintegerAccessor(&HopCountClient::m_size), // <-- 修改类名
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Priority",
                          "Priority assigned to the packets generated.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&HopCountClient::SetPriority, // <-- 修改类名
                                             &HopCountClient::GetPriority), // <-- 修改类名
                          MakeUintegerChecker<uint8_t>())
            .AddTraceSource("Tx",
                            "A packet has been sent",
                            MakeTraceSourceAccessor(&HopCountClient::m_txTrace), // <-- 修改类名
                            "ns3::Packet::AddressTracedCallback");
    return tid;
}

HopCountClient::HopCountClient()// <-- 修改所有后续的类名
{
    NS_LOG_FUNCTION(this);
    m_sent = 0;
    m_socket = nullptr;
    m_sendEvent = EventId();
    m_peerAddressSet = false;
    m_priority = 0;
    m_isFlood = true;
}

HopCountClient::~HopCountClient()
{
    NS_LOG_FUNCTION(this);
}

void
HopCountClient::SetRemote(PacketSocketAddress addr)
{
    NS_LOG_FUNCTION(this << addr);
    m_peerAddress = addr;
    m_peerAddressSet = true;
}

void
HopCountClient::DoDispose()
{
    NS_LOG_FUNCTION(this);
    Application::DoDispose();
}

void
HopCountClient::SetPriority(uint8_t priority)
{
    m_priority = priority;
    if (m_socket)
    {
        m_socket->SetPriority(priority);
    }
}

void
HopCountClient::SetisFlood(bool isFlood)
{
    m_isFlood = isFlood;
}

uint8_t
HopCountClient::GetPriority() const
{
    return m_priority;
}


void
HopCountClient::StartApplication()
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(m_peerAddressSet, "Peer address not set");

    if (!m_socket)
    {
        TypeId tid = TypeId::LookupByName("ns3::PacketSocketFactory");
        m_socket = Socket::CreateSocket(GetNode(), tid);

        m_socket->Bind(m_peerAddress);
        m_socket->Connect(m_peerAddress);

        if (m_priority)
        {
            m_socket->SetPriority(m_priority);
        }
    }

    m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    m_sendEvent = Simulator::ScheduleNow(&HopCountClient::Send, this); // <-- 修改
}

void
HopCountClient::StopApplication()
{
    NS_LOG_FUNCTION(this);
    Simulator::Cancel(m_sendEvent);
    if(m_socket) 
    {
        m_socket->Close();
    }
}

/********************************************************
 * *
 * 核心修改在此处的 Send() 方法             *
 * *
 ********************************************************/
void
HopCountClient::Send()
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT(m_sendEvent.IsExpired());

    // 1. 创建数据包
    //    减去头部大小，确保总数据包大小符合预期
    uint32_t packetSize = m_size;
    HopCountHeader exampleHeader; // 创建一个临时头部以获取其大小
    uint32_t headerSize = exampleHeader.GetSerializedSize();
    if (packetSize < headerSize) {
        packetSize = headerSize; // 确保packetSize至少和头部一样大
    }
    Ptr<Packet> p = Create<Packet>(packetSize - headerSize);
    
    // 2. 创建并设置 HopCountHeader
    HopCountHeader hopHeader;
    hopHeader.SetHopCount(0); // 初始跳数设为0
    hopHeader.SetIsFlood(m_isFlood); 

    // 3. 将头部添加到数据包的前面
    p->AddHeader(hopHeader);        

    
    // 后续逻辑与原来相同
    std::stringstream peerAddressStringStream;
    peerAddressStringStream << PacketSocketAddress::ConvertFrom(m_peerAddress);

    if ((m_socket->Send(p)) >= 0)
    {
        m_txTrace(p, m_peerAddress);
        NS_LOG_INFO("TraceDelay TX " << m_size << " bytes to " << peerAddressStringStream.str()
                                     << " Uid: " << p->GetUid()
                                     << " Time: " << (Simulator::Now()).GetSeconds());
        m_sent++;
    }
    else
    {
        NS_LOG_INFO("Error while sending " << m_size << " bytes to "
                                           << peerAddressStringStream.str());
    }

    if ((m_sent < m_maxPackets) || (m_maxPackets == 0))
    {
        // 调度下一次发送
        m_sendEvent = Simulator::Schedule(m_interval, &HopCountClient::Send, this);
    }
}

} // namespace ns3
