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
  Pronum=0x9999;
  reqlen=acklen=100;
  is_fullmesh_or_torus=false;
}
sendflowclient::~sendflowclient()
{
  for (auto ai:m_socket){
    ai = nullptr;
  }
}


void
sendflowclient::Setup_out_device(uint32_t deviceIndex)
{
  outdeviceIndex = deviceIndex;
}
void
sendflowclient::Setup_pronum(uint16_t pro)
{
  Pronum=pro;
}
void
sendflowclient::addinerdevice(uint32_t deviceIndex)
{
  internaldeviceIndex.push_back(deviceIndex);
}

void sendflowclient::StartApplication()
{

}

void sendflowclient::StopApplication()
{
  for (auto ai:m_socket){
    if (ai) {
      ai->Close();
      ai = 0;
    }
  }
}


void sendflowclient::HandleRead(Ptr<Socket> socket)
{

  Ptr<Packet> packet;
  Address from;

  // --- 在循环外获取 MTU ---
  // 因为在 REQUEST 分支中（逻辑2）需要用 MTU 来计算
  Ptr<Node> node = GetNode();
  Ptr<NetDevice> device;
  if (is_fullmesh_or_torus) device = node->GetDevice(1);
  else device = node->GetDevice(0);
  ThreePHeader tempTPH; // 临时对象用于获取大小
  HopCountHeader tempHH; // 临时对象用于获取大小
  // uint16_t m_mtu = device->GetMtu() - tempTPH.GetSerializedSize() - tempHH.GetSerializedSize();
  uint16_t m_mtu = device->GetMtu();
  // --- MTU 获取结束 ---
  
  
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
      // // 逻辑2: (发送方) 收到 REQUEST。计算期望的 ACT 数量
      // // 这里的计算必须和 SendPacket 中 DATA 循环逻辑一致
      // uint64_t numPackets;
      // if (datasize<m_mtu) numPackets=1;
      // else {
      //   if (datasize % m_mtu==0) numPackets=datasize / m_mtu;
      //   else numPackets = (datasize / m_mtu) + 1;
      // }
        
      // // m_ackPacketsExpected[{tag,{src,dst}}] = numPackets;
      // m_ackPacketsExpected[{tag,{src,dst}}] = 1;
      // m_ackPacketsReceived[{tag,{src,dst}}] = 0; // 初始化 ACT 计数器
      // // NS_LOG_INFO("Flow " << flowid << ": (Sender) Expecting " << numPackets << " ACT packets.");
      // // realsend(packet,from,4,formIndex);
      // if (is_fullmesh_or_torus) SendPacket(ThreePHeader::DATA,from,TPheader.GetDataSize(),tag,src,dst,5,formIndex);
      // else SendPacket(ThreePHeader::DATA,from,TPheader.GetDataSize(),tag,src,dst,4,formIndex);
      // NS_LOG_INFO("[RZV] REQUEST arrived tag="<<tag<<" src="<<src<<" dst="<<dst
      //       <<" time="<<Simulator::Now().GetFemtoSeconds()
      //       <<" has_recvfunc="<<(m_recvfunctable.count({tag,{src,dst}})!=0));
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
              int      ackSendModel = is_fullmesh_or_torus ? 5 : 4;
              int      ackIndex     = formIndex;
              
              ns3::Simulator::ScheduleWithContext((this->GetNode()->GetId()), Seconds(0), [this,ackDest,acksize,ackTag,ackSrc,ackDst,ackSendModel,ackIndex](){
                  SendPacket(ThreePHeader::ACT,ackDest,acksize,ackTag,ackSrc,ackDst,ackSendModel,ackIndex); });              
              // if (is_fullmesh_or_torus)
              // {
              //   SendPacket(ThreePHeader::ACT,from,acklen,tag,src,dst,5,formIndex);                 
              // }
              // else
              // {
              //   SendPacket(ThreePHeader::ACT,from,acklen,tag,src,dst,4,formIndex);
              // }

              m_recvHash[{tag,{src,dst}}]=m_alldatasize[{tag,{src,dst}}];
              if (check_recvfunc(tag,src,dst)){
                fuc_and_par fp = m_recvfunctable[{tag,{src,dst}}];

                AstraSim::RecvPacketEventHadndlerData* ehd = (AstraSim::RecvPacketEventHadndlerData*) fp.fun_arg;
                ehd->flowTag=m_request[{tag,{src,dst}}].flowTag;

                fp.msg_handler(fp.fun_arg); 
                m_recvHash.erase({tag,{src,dst}});
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
sendflowclient::TriggerSendTo_now(PacketSocketAddress dstadd,uint64_t datasize,uint32_t tag,uint32_t src,uint32_t dst,int sendmodel)
{
  // NS_LOG_INFO("[" << this->GetNode()->GetId() << "]" << "TriggerSendTo_now declared now time = "  << Simulator::Now().GetFemtoSeconds()
  //             <<"  sendsize = "<<datasize<<"  dest = "<<dstadd);
  NS_LOG_INFO("[TriSend] src="<<src<<" dst="<<dst<<" tag="<<tag<<" size="<<datasize);
  // SendPacket(ThreePHeader::REQUEST, dstadd, datasize, tag,src,dst,sendmodel);
  if(datasize == 1024)
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
sendflowclient::realsend(Ptr<Packet> packet,const Address& dest,int sendmodel,int mindex)
{
  PacketSocketAddress now_dest=PacketSocketAddress::ConvertFrom(dest);

  if (sendmodel==0){//外部
    now_dest.SetSingleDevice(outdeviceIndex);
    Ptr<Socket> sock=m_socket[m_socket.size()-1];
    sock->SendTo(packet,0,now_dest);
  }
  if (sendmodel==1){//内部（且无nvls）,随机找一个内部出端口扔出去
    auto nowsys = std::chrono::system_clock::now();
    auto duration = nowsys.time_since_epoch();
    uint64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    uint32_t seed = static_cast<uint32_t>(now);
    uint32_t outindex = seed % (m_socket.size()-1);//因为包含一个外部端口
    Ptr<Socket> sock=m_socket[outindex];

    now_dest.SetSingleDevice(internaldeviceIndex[outindex]);
    sock->SendTo(packet,0,now_dest);
  }
  if (sendmodel==2){//内部（nvls）

  }
  if (sendmodel==4){//无条件回送(可能有点小问题)/确实有问题
    int m_index=0;
    if (mindex==-1) m_index=m_socket.size()-1;
    else m_index=mindex;
    // if (m_index==m_socket.size()-1) now_dest.SetSingleDevice(outdeviceIndex);
    // else now_dest.SetSingleDevice(internaldeviceIndex[m_index]);
    now_dest.SetSingleDevice(m_index);

    Ptr<Socket> sock=m_socket[m_index];
    sock->SendTo(packet,0,now_dest);
  }
  if (sendmodel==5){//送本地交换机
    m_bridge->hostsend(this->GetNode()->GetDevice(0),packet,
                        0x9999,m_bridge->GetAddress(),now_dest.GetPhysicalAddress());
  }
}

void
sendflowclient::initialization()
{
    // 创建 PacketSocket（关键）
    TypeId tid = TypeId::LookupByName("ns3::PacketSocketFactory");
    if (is_fullmesh_or_torus){
      Ptr<Socket> sock = Socket::CreateSocket (GetNode(), tid);
      PacketSocketAddress addr;
      addr.SetSingleDevice (0);
      addr.SetProtocol(Pronum);
      sock->Bind (addr);

      sock->SetRecvCallback(MakeCallback(&sendflowclient::HandleRead, this));
      m_socket.push_back (sock);
    }
    else{
      //每一个packetsocket对应一个端口
      for (int i=0;i<internaldeviceIndex.size();i++){//内部交换机端口
        Ptr<Socket> sock = Socket::CreateSocket (GetNode(), tid);
        PacketSocketAddress addr;
        addr.SetSingleDevice (internaldeviceIndex[i]);
        addr.SetProtocol(Pronum);
        sock->Bind (addr);
        
        sock->SetRecvCallback(MakeCallback(&sendflowclient::HandleRead, this));
        m_socket.push_back (sock);
      }
      //外部交换机唯一端口
      Ptr<Socket> sock = Socket::CreateSocket (GetNode(), tid);
      PacketSocketAddress addr;
      addr.SetSingleDevice (outdeviceIndex);
      addr.SetProtocol(Pronum);
      sock->Bind (addr);
      sock->SetRecvCallback(MakeCallback(&sendflowclient::HandleRead, this));
      m_socket.push_back (sock);
    }
    
}

void
sendflowclient::SendPacket(ThreePHeader::State state, const Address& dest, uint64_t datasize,
  uint32_t tag,uint32_t src,uint32_t dst,int sendmodel,int mindex)
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
  //逻辑1：如果是req包，根据mtu计算需要收多少包，并用数据结构存储（根据flowid），接受时根据flowid统计到达个数
  if (state == ThreePHeader::REQUEST) {
      // NS_LOG_INFO("[" << this->GetNode()->GetId() << "]" << "SendPacket running now time = "  << Simulator::Now().GetFemtoSeconds());
      // uint64_t numPackets;
      // if (datasize<m_mtu) numPackets=1;
      // else {
      //   if (datasize % m_mtu==0) numPackets=datasize / m_mtu;
      //   else numPackets = (datasize / m_mtu) + 1;
      // }
      
      // m_dataPacketsExpected[{tag,{src,dst}}] = numPackets;
      // m_dataPacketsReceived[{tag,{src,dst}}] = 0; // 初始化接收计数器

      // NS_LOG_INFO("Flow " << flowid << ": (Receiver) Expecting " << numPackets << " DATA packets.");
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
  
  //逻辑2：如果是发送端，同样计算需要多少包，并用结构存储，但是这个结构用于统计ACK个数，这两个结构分开。
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
        // m_socket->SendTo(packet, 0, dest);
      
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
      // m_socket->SendTo(packet, 0, dest);
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
void
sendflowclient::set_localbridge(Ptr<MinHopBridgeNetDevice> bri){
    m_bridge=bri;
}
void
sendflowclient::set_isfullortorus(bool truth){
    is_fullmesh_or_torus=truth;
}

}