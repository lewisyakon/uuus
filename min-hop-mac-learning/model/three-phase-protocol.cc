#include "three-phase-protocol.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/inet-socket-address.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"


namespace ns3 {

NS_LOG_COMPONENT_DEFINE("ThreePhaseProtocol");

// ---------- 客户端实现 ----------

RequestorApp::RequestorApp()
{
  acklen=100;
  Pronum=0x9999;
  datalen=1024;
}
RequestorApp::~RequestorApp()
{
  m_socket = nullptr;
}


void
RequestorApp::Setup(uint32_t deviceIndex)
{
  m_deviceIndex = deviceIndex;
}
void
RequestorApp::Setup(uint32_t deviceIndex,uint16_t pro)
{
  m_deviceIndex = deviceIndex;
  Pronum=pro;
}
void RequestorApp::setdatalen(int len)
{
  datalen=len;
}
void
RequestorApp::setacklen(int len)
{
  acklen = len;
}
void
RequestorApp::AddServer(PacketSocketAddress serverAddr)
{
  m_servers.push_back(serverAddr);
  // NS_LOG_INFO("Added server: device=" << serverAddr.GetSingleDevice()
  //              << ", proto=" << serverAddr.GetProtocol());
}


void RequestorApp::StartApplication()
{
  // // 创建 PacketSocket（关键）
  // TypeId tid = TypeId::LookupByName("ns3::PacketSocketFactory");
  // m_socket = Socket::CreateSocket(GetNode(), tid);

  // PacketSocketAddress local;
  // local.SetSingleDevice(m_deviceIndex);
  // local.SetProtocol(Pronum);
  // // local.SetPhysicalAddress(Mac48Address::GetBroadcast()); // 接收所有
  // m_socket->Bind(local);

  // // m_socket->Connect(m_peerAddress);
  // m_socket->SetRecvCallback(MakeCallback(&RequestorApp::HandleRead, this));

  // SendRequest();
}

void RequestorApp::StopApplication()
{
  if (m_socket) {
    m_socket->Close();
    m_socket = 0;
  }
}


void RequestorApp::HandleRead(Ptr<Socket> socket)
{
  Ptr<Packet> packet;
  Address from;

  while ((packet = socket->RecvFrom(from))) {
    Ptr<Packet> p = packet->Copy();
    ThreePHeader TPheader;
    HopCountHeader hopHeader;
    p->RemoveHeader(hopHeader);  // 移除头部以读取状态
    p->RemoveHeader(TPheader);  // 移除头部以读取状态
    
    // NS_LOG_INFO("ResponderApp: Received packet with state=" << (int)TPheader.GetState());

    switch (TPheader.GetState())
    {
    case ThreePHeader::REQUEST:
      SendPacket(ThreePHeader::DATA,from);
      break;
    case ThreePHeader::DATA:
      SendPacket(ThreePHeader::ACT,from);
      break;
    case ThreePHeader::ACT:
      NS_LOG_INFO("ResponderApp: Received ACT, final confirmation");
      break;

    default:
      NS_LOG_WARN("Unknown state received");
      break;
    }
  }
}


void
RequestorApp::TriggerSendTo(uint32_t index,Time startTime)
{
  if (index >= m_servers.size()) return;
  // SendPacket(ThreePHeader::REQUEST, m_servers[index].ConvertTo());
  Simulator::Schedule(startTime, &RequestorApp::SendPacket, this,
                      ThreePHeader::REQUEST,
                      m_servers[index]);
}
void
RequestorApp::initialization()
{
    // 创建 PacketSocket（关键）
    TypeId tid = TypeId::LookupByName("ns3::PacketSocketFactory");
    m_socket = Socket::CreateSocket(GetNode(), tid);

    PacketSocketAddress local;
    local.SetSingleDevice(m_deviceIndex);
    local.SetProtocol(Pronum);
    // local.SetPhysicalAddress(Mac48Address::GetBroadcast()); // 接收所有
    m_socket->Bind(local);

    // m_socket->Connect(m_peerAddress);
    m_socket->SetRecvCallback(MakeCallback(&RequestorApp::HandleRead, this));
}

void
RequestorApp::SendPacket(ThreePHeader::State state, const Address& dest)
{
  Ptr<Packet> packet;
  HopCountHeader hopHeader;
  hopHeader.SetHopCount(0); // 初始跳数设为0
  hopHeader.SetIsFlood(false); 
  ThreePHeader TPheader;
  TPheader.SetState(state);
  
  Ptr<Node> node = GetNode();
  Ptr<NetDevice> device = node->GetDevice(0);
  uint16_t m_mtu=device->GetMtu()-TPheader.GetSerializedSize()-hopHeader.GetSerializedSize();
  
  if (state==ThreePHeader::DATA){
    if (datalen>m_mtu){
      int len=datalen;
      for (int i=0;i<=datalen/m_mtu;i++){
        if (len>m_mtu) packet = Create<Packet>(m_mtu);
        else packet = Create<Packet>(len);

        packet->AddHeader(TPheader);
        packet->AddHeader(hopHeader);
        m_socket->SendTo(packet, 0, dest);
      
        len-=m_mtu;
      }
    }
    else {
      packet = Create<Packet>(datalen);
      packet->AddHeader(TPheader);
      packet->AddHeader(hopHeader);
      m_socket->SendTo(packet, 0, dest);
    }
  }
  else {
    if (acklen>m_mtu){
      int len=acklen;
      for (int i=0;i<acklen/m_mtu;i++){
        if (len>m_mtu) packet = Create<Packet>(m_mtu);
        else packet = Create<Packet>(len);

        packet->AddHeader(TPheader);
        packet->AddHeader(hopHeader);
        m_socket->SendTo(packet, 0, dest);
      
        len-=m_mtu;
      }
    }
    else{
      packet = Create<Packet>(acklen);
      packet->AddHeader(TPheader);
      packet->AddHeader(hopHeader);
      m_socket->SendTo(packet, 0, dest);
    }
  }
  
  // NS_LOG_INFO("RequestorApp: Sent " << (state == ThreePHeader::REQUEST ? "REQUEST" : 
  //               state == ThreePHeader::ACT ? "ACT" : "DATA")
  //              << " to server on device " << dest
  //              << ", size=" << packet->GetSize());
}




} // namespace ns3
