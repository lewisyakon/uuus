#ifndef THREE_PHASE_PROTOCOL_H
#define THREE_PHASE_PROTOCOL_H

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


namespace ns3 {

// ---------- 客户端（请求方） ----------
class RequestorApp : public Application
{
public:
  RequestorApp();
  virtual ~RequestorApp();

  void AddServer(PacketSocketAddress serverAddr);  // 添加服务器
  void Setup(uint32_t deviceIndex);
  void Setup(uint32_t deviceIndex,uint16_t pro);

  void setacklen(int len);
  void setdatalen(int len);

  void TriggerSendTo(uint32_t index,Time startTime);     // 发送给指定服务器
  void initialization();

private:
  virtual void StartApplication(void);
  virtual void StopApplication(void);

  void HandleRead(Ptr<Socket> socket); // 处理接收
  void SendPacket(ThreePHeader::State state, const Address& dest);

  

  Ptr<Socket> m_socket;
  std::vector<PacketSocketAddress> m_servers;  // 所有服务器地址

  int datalen;
  int acklen;
  uint32_t m_deviceIndex;
  uint16_t Pronum;
};


} // namespace ns3

#endif // THREE_PHASE_PROTOCOL_H
