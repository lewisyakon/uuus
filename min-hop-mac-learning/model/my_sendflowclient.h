#ifndef MY_SEND_FLOW_CLIENT_H
#define MY_SEND_FLOW_CLIENT_H

#include "astra-sim/system/AstraNetworkAPI.hh"
#include "astra-sim/system/Sys.hh"
#include "astra-sim/system/RecvPacketEventHadndlerData.hh"

#include "ns3/application.h"
#include "ns3/address.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/nstime.h"

#include "ns3/packet-socket.h"           // PacketSocket
#include "ns3/packet-socket-factory.h"
#include "ns3/packet-socket-address.h"
#include "ns3/mac48-address.h"           // MAC 地址
#include "ns3/hop-count-header.h"
#include "3PHeader.h"
#include<map>
#include"min-hop-bridge-net-device.h"
#include "FlowTagHeader.h"


namespace ns3 {
struct fuc_and_par{
  void (*msg_handler)(void *fun_arg);//函数
  void *fun_arg;//参数
};


// ---------- 客户端（请求方） ----------
class sendflowclient : public Application
{
public:
  sendflowclient();
  virtual ~sendflowclient();

  void Setup_out_device(uint32_t deviceIndex);
  void Setup_pronum(uint16_t pro);
  void addinerdevice(uint32_t deviceIndex);

  void TriggerSendTo(PacketSocketAddress dstadd,Time startTime,uint64_t datasize,uint32_t tag,uint32_t src,uint32_t dst,int sendmodel);     // 发送给指定服务器
  void TriggerSendTo_now(PacketSocketAddress dstadd,uint64_t datasize,uint32_t tag,uint32_t src,uint32_t dst,int sendmodel);
  void initialization();

  void set_sendfunc(int tag,int src,int dst,fuc_and_par fp); 
  void write_flowtag(int tag,int src,int dst,AstraSim::sim_request request);
  AstraSim::sim_request get_flowtag(int tag,int src,int dst);

  void set_recvfunc(int tag,int src,int dst,fuc_and_par fp); 
  void erase_recvhash(int tag,int src,int dst);
  bool check_recvfunc(int tag,int src,int dst);
  bool check_recvhash(int tag,int src,int dst);
  uint64_t get_recvhash(int tag,int src,int dst);
  void set_recvhash(int tag,int src,int dst,uint64_t countnum);
  
  void set_localbridge(Ptr<MinHopBridgeNetDevice> bri);
  void set_isfullortorus(bool truth);//判断是否是fullmesh或者torus，如果是，无条件回送要移位

private:
  virtual void StartApplication(void);
  virtual void StopApplication(void);

  void HandleRead(Ptr<Socket> socket); // 处理接收
  void SendPacket(ThreePHeader::State state, const Address& dest,uint64_t datasize,
    uint32_t tag,uint32_t src,uint32_t dst,int sendmodel,int mindex=-1);
  void realsend(Ptr<Packet> packet,const Address& dest,int sendmodel,int mindex=-1);



  std::vector<Ptr<Socket> > m_socket;

  std::map<pair<int,pair<int,int> >, ns3::fuc_and_par> m_sendfunctable;  // key: flowid
  std::map<pair<int,pair<int,int> >,ns3::fuc_and_par>m_recvfunctable;
  std::map<pair<int,pair<int,int> >,AstraSim::sim_request>m_request;
    std::map<pair<int,pair<int,int> >,bool> m_recvfuncflag;


  uint32_t outdeviceIndex;
  std::vector<uint32_t> internaldeviceIndex;
  uint16_t Pronum;
  int reqlen,acklen;
  std::map<pair<int,pair<int,int> >, uint64_t> m_recvHash;

  Ptr<MinHopBridgeNetDevice> m_bridge;

  bool is_fullmesh_or_torus;


  // 预期 DATA 包的总数
  std::map<pair<int,pair<int,int> >, uint64_t> m_dataPacketsExpected;
  // 当前已收到的 DATA 包数量
  std::map<pair<int,pair<int,int> >, uint64_t> m_dataPacketsReceived;


  // 预期 ACT 包的总数
  std::map<pair<int,pair<int,int> >, uint64_t> m_ackPacketsExpected;
  //当前已收到的 ACT 包数量
  std::map<pair<int,pair<int,int> >, uint64_t> m_ackPacketsReceived;

  std::map<pair<int,pair<int,int> >, uint64_t> m_alldatasize;
};


} // namespace ns3

#endif