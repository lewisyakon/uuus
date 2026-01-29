#include "my_sendflowclient.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/inet-socket-address.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include <algorithm>
#include <sstream>
#include <chrono>


namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SendFlowClient");


// ---------- 客户端实现 ----------

sendflowclient::sendflowclient()
{
  reqlen=acklen=100;
  localbridgeindex=0;
}
sendflowclient::~sendflowclient()
{
}

void sendflowclient::StartApplication()
{

}

void sendflowclient::StopApplication()
{
}


void sendflowclient::HandleRead(Ptr<Socket> socket)
{

  Ptr<Packet> packet;
  Address from;

  // --- 在循环外获取 MTU ---
  Ptr<Node> node = GetNode();
  Ptr<NetDevice> device;
  device=node->GetDevice(localbridgeindex);
  ThreePHeader tempTPH; // 临时对象用于获取大小
  HopCountHeader tempHH; // 临时对象用于获取大小
  // uint16_t m_mtu = device->GetMtu() - tempTPH.GetSerializedSize() - tempHH.GetSerializedSize();
  uint16_t m_mtu = device->GetMtu();

  while ((packet = socket->RecvFrom(from))) {
    Ptr<Packet> p = packet->Copy();
    ThreePHeader TPheader;
    HopCountHeader hopHeader;
    FlowTagHeader FTHeader;
    p->RemoveHeader(hopHeader);  // 移除头部以读取状态
    p->RemoveHeader(TPheader);  // 移除头部以读取状态
    
    // 获取flowid，用作map的key
    uint32_t tag = TPheader.GetTag();
    uint32_t src = TPheader.GetSrc();
    uint32_t dst = TPheader.GetDst();
    uint64_t datasize = TPheader.GetDataSize(); // 获取数据大小



    Address local;
    socket->GetSockName(local);
    PacketSocketAddress laddr = PacketSocketAddress::ConvertFrom(local);
    uint32_t formIndex = laddr.GetSingleDevice(); //拿到来源的netdevice编号

    // NS_LOG_DEBUG("[" << node->timeGetId() << "] Received packet for flow " << flowid
    //                  << " from " << from
    //                  << " with state=" << (int)TPheader.GetState()
    //                  << ", header datasize=" << datasize
    //                  << ", actual packet size=" << packet->GetSize());

    switch (TPheader.GetState())
    {
    case ThreePHeader::REQUEST:
    {
      if(check_recvfunc(tag,src,dst) == false){
        m_recvfuncflag[{tag,{src,dst}}] = 1;
        return ;
      }
      fuc_and_par fp = m_recvfunctable[{tag,{src,dst}}];
      fp.msg_handler(fp.fun_arg); 
      m_recvfunctable.erase({tag,{src,dst}});
    }
    break;

    case ThreePHeader::DATA:
    {
      p->RemoveHeader(FTHeader);
      AstraSim::sim_request now_request;
      FTHeader.ToFlowTag(now_request.flowTag);
      uint32_t PacketNum=FTHeader.get_packetnum();
      if (m_request.find({tag,{src,dst}})==m_request.end()) m_request[{tag,{src,dst}}]=now_request;
      if (m_dataPacketsExpected.find({tag,{src,dst}})==m_dataPacketsExpected.end()){
        m_dataPacketsExpected[{tag,{src,dst}}]=PacketNum;
        m_alldatasize[{tag,{src,dst}}]=TPheader.GetDataSize();
      } 

      // 逻辑1 (计数): (接收方) 收到 DATA
      if (m_dataPacketsExpected.count({tag,{src,dst}})!=0) {
          m_dataPacketsReceived[{tag,{src,dst}}]++;
          // realsend(packet,from,4,formIndex);
          // SendPacket(ThreePHeader::ACT,from,acklen,tag,src,dst,4,formIndex);  
          // 检查是否是最后一个 DATA 包
          if (m_dataPacketsReceived[{tag,{src,dst}}] == m_dataPacketsExpected[{tag,{src,dst}}]) {
              // NS_LOG_INFO("Flow " << flowid << ": (Receiver) LAST DATA packet received (" << m_dataPacketsReceived[flowid] << "/" << m_dataPacketsExpected[flowid] << ").");
              // --- 填充recvHash ---
              Address ackDest = from;                          // 目的地址
              uint64_t acksize = acklen;
              uint32_t ackTag  = tag;
              uint32_t ackSrc  = src;
              uint32_t ackDst  = dst;
              int      ackIndex     = formIndex;
              ns3::Simulator::ScheduleWithContext((this->GetNode()->GetId()), Seconds(0), [this,ackDest,acksize,ackTag,ackSrc,ackDst,ackIndex](){
                  SendPacket(ThreePHeader::ACT,ackDest,acksize,ackTag,ackSrc,ackDst,FlowSendMode::Loopback,ackIndex); });              


              m_recvHash[{tag,{src,dst}}]=m_alldatasize[{tag,{src,dst}}];
              if (check_recvfunc(tag,src,dst)){
                fuc_and_par fp = m_recvfunctable[{tag,{src,dst}}];

                AstraSim::RecvPacketEventHadndlerData* ehd = (AstraSim::RecvPacketEventHadndlerData*) fp.fun_arg;
                ehd->flowTag=m_request[{tag,{src,dst}}].flowTag;

                fp.msg_handler(fp.fun_arg); 
                m_recvHash.erase({tag,{src,dst}});
                m_recvfunctable.erase({tag,{src,dst}});
                m_request.erase({tag,{src,dst}});
                NS_LOG_INFO("{tag,src,dst}= " << tag<<"  "<<src<<" "<<dst 
                  << " 接收完成; TIME= "<<Simulator::Now().GetFemtoSeconds());
              }
              // NS_LOG_INFO("flowid= " << flowid << " 接收完成 ");
              
              // 清理map
              m_dataPacketsExpected.erase({tag,{src,dst}});
              m_dataPacketsReceived.erase({tag,{src,dst}});
          }
      } else {
          NS_LOG_WARN("{tag,src,dst}= " << tag<<"  "<<src<<" "<<dst << ": Received DATA packet without expected count setup.");
      }

    }
    break;
    case ThreePHeader::ACT:
    {
      // 逻辑2 (计数): (发送方) 收到 ACT (ACK)
      // NS_LOG_INFO("ResponderApp: Received ACT, final confirmation"); // 原始日志
      if (m_ackPacketsExpected.find({tag,{src,dst}}) != m_ackPacketsExpected.end()) {
          m_ackPacketsReceived[{tag,{src,dst}}]++;
            
          // 检查是否是最后一个 ACT 包
          if (m_ackPacketsReceived[{tag,{src,dst}}] == m_ackPacketsExpected[{tag,{src,dst}}]) {
              // NS_LOG_INFO("Flow " << flowid << ": (Sender) LAST ACT packet received (" << m_ackPacketsReceived[flowid] << "/" << m_ackPacketsExpected[flowid] << ").");
              // (未来引用notify_send回调函数的地方)
              if (m_sendfunctable.find({tag,{src,dst}}) != m_sendfunctable.end()) {
                  fuc_and_par fp = m_sendfunctable[{tag,{src,dst}}];
                  fp.msg_handler(fp.fun_arg); 
                  m_sendfunctable.erase({tag,{src,dst}});
                  NS_LOG_INFO("{tag,src,dst}= " << tag<<"  "<<src<<" "<<dst 
                    << " 发送完成; TIME= "<<Simulator::Now().GetFemtoSeconds());
              } else {
                  NS_LOG_WARN("{tag,src,dst}= " << tag<<"  "<<src<<" "<<dst <<": (Sender) Callback not found for flowid.");
              }
                
              // 清理map
              m_ackPacketsExpected.erase({tag,{src,dst}});
              m_ackPacketsReceived.erase({tag,{src,dst}});
          }
      } else {
          NS_LOG_WARN("{tag,src,dst}= " << tag<<"  "<<src<<" "<<dst << ": Received ACT packet without expected count setup.");
      }
    }
    break;

  default:
    NS_LOG_WARN("Unknown state received");
    break;
    }
  }
}

void
sendflowclient::TriggerSendTo_now(PacketSocketAddress dstadd,uint64_t datasize,uint32_t tag,uint32_t src,uint32_t dst,FlowSendMode sendmodel)
{
  NS_LOG_INFO("TriggerSendTo_now declared now time = "  << Simulator::Now().GetFemtoSeconds()<<" src="<<src<<" dst="<<dst<<" tag="<<tag<<" size="<<datasize);
  if(datasize == 1023)
  {
    ns3::Simulator::ScheduleWithContext((this->GetNode()->GetId()), Seconds(0), [this,dstadd,datasize,tag,src,dst,sendmodel](){
      SendPacket(ThreePHeader::REQUEST, dstadd, datasize, tag,src,dst,sendmodel);});
  }
  else
  {
    ns3::Simulator::ScheduleWithContext((this->GetNode()->GetId()), Seconds(0), [this,dstadd,datasize,tag,src,dst,sendmodel](){
      SendPacket(ThreePHeader::DATA, dstadd, datasize, tag,src,dst,sendmodel);});    
  }
}
void
sendflowclient::realsend(Ptr<Packet> packet,const Address& dest,FlowSendMode sendmodel,int mindex)
{
    m_io->Send(dest,packet,sendmodel,mindex);
}


void
sendflowclient::initialization(uint16_t Pronum,uint32_t m_outDev,uint32_t m_innerDev,Ptr<NetDevice> bridgenode)
{
    localbridgeindex=m_innerDev;
    if (!m_io) m_io = ns3::CreateObject<ns3::FlowIo>();
    m_io->SetNode(GetNode());
    m_io->SetProtocol(Pronum);
    m_io->SetOutDeviceIndex(m_outDev);
    m_io->SetInnerDeviceIndex(m_innerDev);   // 一般固定=0
    m_io->SetLocalBridge(bridgenode);            // 保证它就是 device0
    m_io->SetRxBindMode(RxBindMode::BridgeAndExternal);

    m_io->Start(MakeCallback(&sendflowclient::HandleRead, this));
}
void
sendflowclient::initialization(uint16_t Pronum,uint32_t m_innerDev,Ptr<NetDevice> bridgenode)
{
    localbridgeindex=m_innerDev;
    if (!m_io) m_io = ns3::CreateObject<ns3::FlowIo>();
    m_io->SetNode(GetNode());
    m_io->SetProtocol(Pronum);
    m_io->SetInnerDeviceIndex(m_innerDev);   // 一般固定=0
    m_io->SetLocalBridge(bridgenode);            // 保证它就是 device0
    m_io->SetRxBindMode(RxBindMode::BridgeOnly);

    m_io->Start(MakeCallback(&sendflowclient::HandleRead, this));
}

void
sendflowclient::SendPacket(ThreePHeader::State state, const Address& dest, uint64_t datasize,
  uint32_t tag,uint32_t src,uint32_t dst,FlowSendMode sendmodel,int mindex)
{
  // NS_LOG_INFO("[" << this->GetNode()->GetId() << "]" << "SendPacket running now time = "  << Simulator::Now().GetFemtoSeconds());
  //这里需要先判断目标点和本机是否在一个机内

  Ptr<Packet> packet;
  HopCountHeader hopHeader;
  hopHeader.SetHopCount(0); // 初始跳数设为0
  hopHeader.SetIsFlood(false); 
  ThreePHeader TPheader;
  TPheader.SetState(state);
  TPheader.SetTag(tag);
  TPheader.SetSrc(src);
  TPheader.SetDst(dst);
  FlowTagHeader FTHeader;
  
  Ptr<Node> node = GetNode();
  Ptr<NetDevice> device = node->GetDevice(0);
  // uint16_t m_mtu=device->GetMtu()-TPheader.GetSerializedSize()-hopHeader.GetSerializedSize();
  uint16_t m_mtu=device->GetMtu();
 
  if (state == ThreePHeader::REQUEST) {
      uint16_t real_packet_size=reqlen-TPheader.GetSerializedSize()-hopHeader.GetSerializedSize();
      packet = Create<Packet>(real_packet_size);
      TPheader.SetDataSize(datasize);
      packet->AddHeader(TPheader);
      packet->AddHeader(hopHeader);
      realsend(packet,dest,sendmodel);

      fuc_and_par fp = m_sendfunctable[{tag,{src,dst}}];
      fp.msg_handler(fp.fun_arg);
      m_sendfunctable.erase({tag,{src,dst}});

      // m_alldatasize[{tag,{src,dst}}]=datasize;
  }
  
  if (state==ThreePHeader::DATA){
    uint64_t numPackets;
    if (datasize<m_mtu) numPackets=1;
    else {
      if (datasize % m_mtu==0) numPackets=datasize / m_mtu;
      else numPackets = (datasize / m_mtu) + 1;
    }
    // m_ackPacketsExpected[{tag,{src,dst}}] = numPackets;
    // 发送前记录act
    m_ackPacketsExpected[{tag,{src,dst}}] = 1;
    m_ackPacketsReceived[{tag,{src,dst}}] = 0; // 初始化 ACT 计数器
    FTHeader.FromFlowTag(get_flowtag(tag,src,dst).flowTag);
    FTHeader.set_packetnum(numPackets);
    TPheader.SetDataSize(datasize);

    int len=datasize;
    for (int i=1;i<=numPackets;i++){
        if (len>=m_mtu){
          uint16_t real_packet_size=m_mtu-TPheader.GetSerializedSize()
          -hopHeader.GetSerializedSize()-FTHeader.GetSerializedSize();
          packet = Create<Packet>(real_packet_size);
          
        } 
        else {
          uint16_t real_packet_size=len-TPheader.GetSerializedSize()
          -hopHeader.GetSerializedSize()-FTHeader.GetSerializedSize();
          packet = Create<Packet>(real_packet_size);
        }
        packet->AddHeader(FTHeader);
        packet->AddHeader(TPheader);
        packet->AddHeader(hopHeader);
        
        realsend(packet,dest,sendmodel,mindex);
      
        len-=m_mtu;
    }
  }
  if (state==ThreePHeader::ACT){
      uint16_t real_packet_size=acklen-TPheader.GetSerializedSize()-hopHeader.GetSerializedSize();
      packet = Create<Packet>(real_packet_size);
      TPheader.SetDataSize(acklen);
      packet->AddHeader(TPheader);
      packet->AddHeader(hopHeader);
      realsend(packet,dest,sendmodel,mindex);
  }
    
  
  
  // NS_LOG_INFO("sendflowclient: Sent " << (state == ThreePHeader::REQUEST ? "REQUEST" : 
  //               state == ThreePHeader::ACT ? "ACT" : "DATA")
  //              << " to server on device " << dest
  //              << ", size=" << packet->GetSize());
}
void
sendflowclient::set_sendfunc(int tag,int src,int dst,fuc_and_par fp){
    m_sendfunctable[{tag,{src,dst}}]=fp; 
}

void
sendflowclient::set_recvfunc(int tag,int src,int dst,fuc_and_par fp){
    if(m_recvfuncflag[{tag,{src,dst}}] == 1){
      m_recvfuncflag[{tag,{src,dst}}] = 0;

      // NS_LOG_INFO("[RZV] CALLBACK fired tag="<<tag<<" src="<<src<<" dst="<<dst);
      
      fp.msg_handler(fp.fun_arg);
      return ;
    }
    m_recvfunctable[{tag,{src,dst}}]=fp; 
}

bool
sendflowclient::check_recvfunc(int tag,int src,int dst){
    if (m_recvfunctable.find({tag,{src,dst}})!=m_recvfunctable.end()) return true;
    else return false;
}
void
sendflowclient::set_recvhash(int tag,int src,int dst,uint64_t countnum){
    m_recvHash[{tag,{src,dst}}]=countnum;
}
void
sendflowclient::erase_recvhash(int tag,int src,int dst){
    m_recvHash.erase({tag,{src,dst}});
}
bool
sendflowclient::check_recvhash(int tag,int src,int dst){
    if (m_recvHash.find({tag,{src,dst}})!=m_recvHash.end()) return true;
    else return false;
}
uint64_t
sendflowclient::get_recvhash(int tag,int src,int dst){
    return m_recvHash[{tag,{src,dst}}];
}
void
sendflowclient::write_flowtag(int tag,int src,int dst,AstraSim::sim_request request){
    m_request[{tag,{src,dst}}]=request;
}
AstraSim::sim_request
sendflowclient::get_flowtag(int tag,int src,int dst){
  return m_request[{tag,{src,dst}}];
}


}