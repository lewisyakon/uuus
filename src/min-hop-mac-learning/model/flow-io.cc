#include "flow-io.h"

#include "ns3/log.h"
#include "ns3/ptr.h" // PeekPointer
#include "ns3/node.h"

#include "ns3/packet-socket-address.h"
#include "ns3/packet-socket-factory.h"

#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("FlowIo");
NS_OBJECT_ENSURE_REGISTERED(FlowIo);

TypeId
FlowIo::GetTypeId()
{
  static TypeId tid =
      TypeId("ns3::FlowIo")
          .SetParent<Object>()
          .SetGroupName("Applications")
          .AddConstructor<FlowIo>();
  return tid;
}

FlowIo::FlowIo() = default;

FlowIo::~FlowIo()
{
  // 资源释放在 DoDispose
}

void
FlowIo::DoDispose()
{
  Stop();
  m_node = nullptr;
  m_bridge = nullptr;
  Object::DoDispose();
}

void
FlowIo::SetNode(Ptr<Node> node)
{
  m_node = node;
}

void
FlowIo::SetProtocol(uint16_t etherType)
{
  m_proto = etherType;
}

void
FlowIo::SetOutDeviceIndex(uint32_t outDevIndex)
{
  m_outDev = outDevIndex;
}

void
FlowIo::SetInnerDeviceIndex(uint32_t innerDevIndex)
{
  m_innerDev = innerDevIndex;
}

void
FlowIo::SetLocalBridge(Ptr<NetDevice> bridgeDev)
{
  m_bridge = bridgeDev;

  // 尝试在 node 已设置时，从指针推断 innerDev（更鲁棒）
  if (m_node && m_bridge)
  {
    for (uint32_t i = 0; i < m_node->GetNDevices(); ++i)
    {
      if (m_node->GetDevice(i) == m_bridge)
      {
        m_innerDev = i;
        break;
      }
    }
  }
}

void
FlowIo::SetRxBindMode(RxBindMode mode)
{
  m_bindMode = mode;
}

void
FlowIo::Start(const SocketRxCallback& cb)
{
  NS_ASSERT_MSG(m_node != nullptr, "FlowIo: node not set");
  NS_ASSERT_MSG(m_proto != 0, "FlowIo: protocol not set");
  NS_ASSERT_MSG(m_node->GetNDevices() >= 1, "FlowIo: node has no devices");
  NS_ASSERT_MSG(m_innerDev < m_node->GetNDevices(), "FlowIo: innerDevIndex out of range");

  // 常规拓扑才要求 outDev 合法；BridgeOnly 可以不设置 outDev
  if (m_bindMode == RxBindMode::BridgeAndExternal)
  {
    NS_ASSERT_MSG(m_outDev < m_node->GetNDevices(), "FlowIo: outDevIndex out of range");
  }

  m_sockRxCb = cb;

  // 若未设置 bridge 指针，则尝试用 innerDevIndex 推断
  if (!m_bridge)
  {
    m_bridge = m_node->GetDevice(m_innerDev);
  }

  // 防止重复 Start
  Stop();

  std::vector<uint32_t> bindDevs;
  bindDevs.push_back(m_innerDev);

  if (m_bindMode == RxBindMode::BridgeAndExternal)
  {
    bindDevs.push_back(m_outDev);
  }

  std::sort(bindDevs.begin(), bindDevs.end());
  bindDevs.erase(std::unique(bindDevs.begin(), bindDevs.end()), bindDevs.end());

  for (uint32_t devIdx : bindDevs)
  {
    NS_ASSERT_MSG(devIdx < m_node->GetNDevices(), "FlowIo: bind devIdx out of range");

    Ptr<Socket> sock = Socket::CreateSocket(m_node, PacketSocketFactory::GetTypeId());

    PacketSocketAddress local;
    local.SetSingleDevice(devIdx);
    local.SetPhysicalAddress(m_node->GetDevice(devIdx)->GetAddress());
    local.SetProtocol(m_proto);

    sock->Bind(local);
    sock->BindToNetDevice(m_node->GetDevice(devIdx));

    // 关键：直接绑定用户原来的 HandleRead(Ptr<Socket>)
    sock->SetRecvCallback(m_sockRxCb);

    m_socks.push_back(sock);
    m_sock2dev[PeekPointer(sock)] = devIdx;
  }
}

void
FlowIo::Stop()
{
  for (auto& s : m_socks)
  {
    if (s)
    {
      s->Close();
    }
  }
  m_socks.clear();
  m_sock2dev.clear();
}


void
FlowIo::SendOnDevice(uint32_t devIndex, const Address& dest, Ptr<Packet> pkt)
{
  NS_ASSERT_MSG(m_node != nullptr, "FlowIo: node not set");
  NS_ASSERT_MSG(pkt != nullptr, "FlowIo: null packet");
  NS_ASSERT_MSG(devIndex < m_node->GetNDevices(), "FlowIo: devIndex out of range");

  Ptr<Socket> sock = nullptr;
  for (auto& s : m_socks)
  {
    auto it = m_sock2dev.find(PeekPointer(s));
    if (it != m_sock2dev.end() && it->second == devIndex)
    {
      sock = s;
      break;
    }
  }
  NS_ASSERT_MSG(sock != nullptr, "FlowIo: no socket bound to requested devIndex");

  PacketSocketAddress ps = PacketSocketAddress::ConvertFrom(dest);
  ps.SetSingleDevice(devIndex);
  ps.SetProtocol(m_proto);

  sock->SendTo(pkt, 0, ps);
}

void
FlowIo::Send(const Address& dest,
             Ptr<Packet> pkt,
             FlowSendMode mode,
             uint32_t rxInDevIndexForLoopback)
{
  NS_ASSERT_MSG(m_node != nullptr, "FlowIo: node not set");
  NS_ASSERT_MSG(pkt != nullptr, "FlowIo: null packet");

  uint32_t dev = m_innerDev;

  // BridgeOnly：没有独立外部口，External 不应被调用（否则说明上层逻辑配置错）
  if (m_bindMode == RxBindMode::BridgeOnly)
  {
    if (mode == FlowSendMode::External)
    {
      NS_ASSERT_MSG(false, "FlowIo: External send requested but RxBindMode=BridgeOnly (no external port)");
    }
    // Internal/Loopback 都走 innerDev
    dev = m_innerDev;
    SendOnDevice(dev, dest, pkt);
    return;
  }

  // BridgeAndExternal：正常三模式
  switch (mode)
  {
  case FlowSendMode::External:
    dev = m_outDev;
    break;

  case FlowSendMode::Internal:
    dev = m_innerDev;
    break;

  case FlowSendMode::Loopback:
    NS_ASSERT_MSG(rxInDevIndexForLoopback != UINT32_MAX,
                  "FlowIo: Loopback requires rxInDevIndexForLoopback");
    dev = (rxInDevIndexForLoopback == m_outDev) ? m_outDev : m_innerDev;
    break;

  default:
    NS_ASSERT_MSG(false, "FlowIo: unknown FlowSendMode");
  }

  SendOnDevice(dev, dest, pkt);
}

} // namespace ns3
