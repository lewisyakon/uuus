#pragma once

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/node.h"
#include "ns3/socket.h"
#include "ns3/net-device.h"
#include "ns3/address.h"
#include "ns3/packet.h"
#include "ns3/callback.h"

#include <unordered_map>
#include <vector>
#include <cstdint>

namespace ns3 {

// 发送语义：按你最新定义的三种
enum class FlowSendMode : uint8_t
{
  External = 0, // 送外部：outdeviceIndex（项目中通常=1）
  Internal = 1, // 送内部：本地交换机/bridge（由 m_innerDev 指定，不再写死 0）
  Loopback = 2  // 无条件回送：外部来的送外部，内部来的送内部
};

// 接收绑定语义：按你最新要求只有两种
enum class RxBindMode : uint8_t
{
  BridgeOnly = 0,        // 特殊拓扑：没有“独立外部口”，只绑内部 bridge 设备（避免重复回调）
  BridgeAndExternal = 1  // 常规拓扑：同时绑内部 bridge 与外部口
};

class FlowIo : public Object
{
public:
  // 直接绑定原来的 HandleRead(Ptr<Socket>)
  using SocketRxCallback = Callback<void, Ptr<Socket>>;

  static TypeId GetTypeId();

  FlowIo();
  ~FlowIo() override;

  // --- 配置（Start 前调用）---
  void SetNode(Ptr<Node> node);
  void SetProtocol(uint16_t etherType);

  void SetOutDeviceIndex(uint32_t outDevIndex);  // 外部口编号（一般=1）
  void SetInnerDeviceIndex(uint32_t innerDevIndex); // 内部 bridge 设备编号（替代写死 0）
  void SetLocalBridge(Ptr<NetDevice> bridgeDev); // 可选：保存指针，提高鲁棒性

  void SetRxBindMode(RxBindMode mode);

  // --- 生命周期 ---
  void Start(const SocketRxCallback& cb); // 创建/绑定 sockets，直接设置 cb 为 recv callback
  void Stop();

  // --- 发送 ---
  void Send(const Address& dest,
            Ptr<Packet> pkt,
            FlowSendMode mode,
            uint32_t rxInDevIndexForLoopback = UINT32_MAX);

private:
  void SendOnDevice(uint32_t devIndex, const Address& dest, Ptr<Packet> pkt);

  void DoDispose() override;

private:
  Ptr<Node> m_node;
  uint16_t  m_proto = 0x9999;

  uint32_t m_outDev   = 1; // 外部口 index
  uint32_t m_innerDev = 0; // 内部 bridge 设备 index（你新增的）
  Ptr<NetDevice> m_bridge; // 内部 bridge 指针（可选保留）

  RxBindMode m_bindMode = RxBindMode::BridgeAndExternal;
  SocketRxCallback m_sockRxCb;

  std::vector<Ptr<Socket>> m_socks;
  std::unordered_map<Socket*, uint32_t> m_sock2dev; // PeekPointer(sock) -> devIndex
};

} // namespace ns3
