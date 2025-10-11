/* 
  CSMA 交换机的“交换机内部延迟”演示
*/
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/bridge-module.h"
#include "ns3/applications-module.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/mac48-address.h"
#include "ns3/tag.h"
#include "ns3/bridge-net-device.h"  // ← 显式包含，确保看到基类声明

#include <iostream>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SwitchIngressEgressDemo");

/* =========================
 *  Egress QueueDisc（端口出方向，保留）
 * ========================= */
class SimpleEgressQueueDisc : public QueueDisc
{
public:
  static TypeId GetTypeId() {
    static TypeId tid = TypeId("ns3::SimpleEgressQueueDisc")
      .SetParent<QueueDisc>()
      .SetGroupName("TrafficControl")
      .AddConstructor<SimpleEgressQueueDisc>()
      .AddAttribute("MaxSize",
                    "Maximum size of the queue disc",
                    QueueSizeValue(QueueSize("100p")),
                    MakeQueueSizeAccessor(&SimpleEgressQueueDisc::m_maxSize),
                    MakeQueueSizeChecker());
    return tid;
  }
  static constexpr const char* LIMIT_EXCEEDED_DROP = "Queue disc limit exceeded";

private:
  bool DoEnqueue(Ptr<QueueDiscItem> item) override
  {
    if (GetCurrentSize() >= m_maxSize)
    {
      DropBeforeEnqueue(item, LIMIT_EXCEEDED_DROP);
      return false;
    }
    bool ok = GetInternalQueue(0)->Enqueue(item);
    if (!ok)
    {
      DropBeforeEnqueue(item, "Dropped by internal queue");
      return false;
    }
    return true;
  }

  Ptr<QueueDiscItem> DoDequeue() override
  {
    if (GetInternalQueue(0)->IsEmpty())
    {
      return nullptr;
    }
    return GetInternalQueue(0)->Dequeue();
  }

  Ptr<const QueueDiscItem> DoPeek() override
  {
    if (GetInternalQueue(0)->IsEmpty())
    {
      return nullptr;
    }
    return GetInternalQueue(0)->Peek();
  }

  bool CheckConfig() override
  {
    if (GetNInternalQueues() == 0)
    {
      AddInternalQueue(CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>(
          "MaxSize", QueueSizeValue(QueueSize("100000p")))); // 注意是 MaxSize
    }
    return true;
  }

  void InitializeParams() override {}

  QueueSize m_maxSize;
};
NS_OBJECT_ENSURE_REGISTERED(SimpleEgressQueueDisc);




class TraceIdTag : public ns3::Tag
{
public:
  static ns3::TypeId GetTypeId() {
    static ns3::TypeId tid = ns3::TypeId("ns3::TraceIdTag")
      .SetParent<ns3::Tag>()
      .AddConstructor<TraceIdTag>();
    return tid;
  }
  ns3::TypeId GetInstanceTypeId() const override { return GetTypeId(); }
  uint32_t GetSerializedSize() const override { return 8; }
  void Serialize(ns3::TagBuffer buf) const override { buf.WriteU64(m_id); }
  void Deserialize(ns3::TagBuffer buf) override { m_id = buf.ReadU64(); }
  void Print(std::ostream &os) const override { os << m_id; }
  void Set(uint64_t id) { m_id = id; }
  uint64_t Get() const { return m_id; }

private:
  uint64_t m_id = 0;
};


/* =========================
 *  自定义桥：带“交换机内部延迟”的 Bridge
 *  （替代原来的 Ingress 方案）
 * ========================= */
class DelayBridgeNetDevice : public BridgeNetDevice
{
  public:
    static TypeId GetTypeId()
    {
      static TypeId tid = TypeId("ns3::DelayBridgeNetDevice")
        .SetParent<BridgeNetDevice>()
        .SetGroupName("Bridge")
        .AddConstructor<DelayBridgeNetDevice>()//增加两个可配置的属性，并给默认值
        // ← Time 类型的属性：SwitchDelay
        .AddAttribute("SwitchDelay",
        "延迟",
        TimeValue(Time("0us")),
        MakeTimeAccessor(&DelayBridgeNetDevice::m_switchDelay),
        MakeTimeChecker())
        // ← Bool 类型的属性：TraceSwitchDelay
        .AddAttribute("TraceSwitchDelay",
        "在交换机进入/转发时打印时间戳以显示内部延迟",
        BooleanValue(false),
        MakeBooleanAccessor(&DelayBridgeNetDevice::m_trace),
        MakeBooleanChecker());
        return tid;
      }

      void AddBridgePort(Ptr<NetDevice> port)
      {
        // 先走基类逻辑（注册端口、FDB 等）
        BridgeNetDevice::AddBridgePort(port);//保证基础设置
        // 再把端口的 Promisc 回调覆盖成我们自己的
        port->SetPromiscReceiveCallback(MakeCallback(&DelayBridgeNetDevice::PromiscRx, this));//修改回调函数
         // 端口发送时刻（MAC 层）
        port->TraceConnectWithoutContext(
            "MacTx",
            MakeCallback(&DelayBridgeNetDevice::MacTxTrace, this, port));

        // 若是 CSMA 端口，则再接物理层“开始发”时刻
        Ptr<CsmaNetDevice> csma = port->GetObject<CsmaNetDevice>();
        if (csma)
        {
          csma->TraceConnectWithoutContext(
              "PhyTxBegin",
              MakeCallback(&DelayBridgeNetDevice::PhyTxBeginTrace, this, port));
        }
      }


  private:
    static const uint32_t kTraceFirstN = 5; //只看前5个帧
    uint64_t m_nextTraceId = 1;   // 分配给帧的唯一 id
    uint32_t m_tracedCount = 0;   // 已经打了多少个帧的标签

    //端口收到帧的回调（必然，最“入”）
    void MacTxTrace( Ptr<NetDevice> outPort, Ptr<const Packet> p)
    {
      TraceIdTag tag;
      if (p->PeekPacketTag(tag)) {
        NS_LOG_UNCOND("SW egress(MacTx) t=" << Simulator::Now().GetSeconds()
                        << " portIfIndex=" << outPort->GetIfIndex()
                        << " id=" << tag.Get());
      }
    }

    // 物理层真正开始发（CSMA 的退避完成）的回调（可选，更“发出”）
    void PhyTxBeginTrace( Ptr<NetDevice> outPort, Ptr<const Packet> p)
    {
      TraceIdTag tag;
      if (p->PeekPacketTag(tag)) {
        NS_LOG_UNCOND("SW egress(PhyTxBegin) t=" << Simulator::Now().GetSeconds()
                        << " portIfIndex=" << outPort->GetIfIndex()
                        << " id=" << tag.Get());
      }
    }

    bool PromiscRx(Ptr<NetDevice> device,
                    Ptr<const Packet> packet,
                    uint16_t protocol,
                    const Address& src,
                    const Address& dst,
                    NetDevice::PacketType packetType)
    {

      bool markThis = (m_tracedCount < kTraceFirstN);

      if (m_trace)
      {
        NS_LOG_UNCOND("进入交换机" << Simulator::Now().GetSeconds()
                        << "包ID" << device->GetIfIndex()
                        << (markThis ? "跟踪" : ""));
      }


      if (m_switchDelay.IsZero())//如果设置没有延迟时间
      {
      // 无内部延迟：直接交给基类桥去处理
        BridgeNetDevice::ReceiveFromDevice(device, packet, protocol, src, dst, packetType);
        return true;
      }

      // 有内部延迟：拷贝一份包，延后再交给基类处理
      Ptr<Packet> copy = packet->Copy();

      if (markThis)
      {
        TraceIdTag tag; tag.Set(m_nextTraceId++);
        copy->AddPacketTag(tag);
        ++m_tracedCount;
      }

      Simulator::Schedule(
        m_switchDelay, //在当前仿真时间+m_switchDelay 后执行
        &DelayBridgeNetDevice::DelayedForward, this,
        device, copy, protocol, src, dst, packetType
      );
      return true;
    }
    void DelayedForward(Ptr<NetDevice> device,
                        Ptr<const Packet> packet,
                        uint16_t protocol,
                        const Address& src,
                        const Address& dst,
                        NetDevice::PacketType packetType)
    {
      if (m_trace)//如果延迟
      {
        TraceIdTag tag; bool has = packet->PeekPacketTag(tag);
        NS_LOG_UNCOND("延迟后时间" << Simulator::Now().GetSeconds()
                        << "延迟时间" << m_switchDelay.GetSeconds()
                        << (has ? (" id=" + std::to_string(tag.Get())) : ""));
      }
      BridgeNetDevice::ReceiveFromDevice(device, packet, protocol, src, dst, packetType);
    }
    Time m_switchDelay;
    bool m_trace = false;
};
NS_OBJECT_ENSURE_REGISTERED(DelayBridgeNetDevice);

/* 简单的 Helper：把端口装到 DelayBridge 上 */
class DelayBridgeHelper
{
  public:
    NetDeviceContainer Install(Ptr<Node> node, const NetDeviceContainer& ports)
    {
      Ptr<DelayBridgeNetDevice> bridge = CreateObject<DelayBridgeNetDevice>();
      node->AddDevice(bridge);
      for (uint32_t i = 0; i < ports.GetN(); ++i)
      {
          bridge->AddBridgePort(ports.Get(i));
      }
      NetDeviceContainer ret; ret.Add(bridge); return ret;
    }
};

// 长度跟踪（没用）
static void EgressQTracer(uint32_t /*oldV*/, uint32_t newV)
{
  NS_LOG_UNCOND("Egress qlen=" << newV);
}

/* =========================
 *  主程序：A — Switch — B（CSMA 端口）
 * ========================= */
int main(int argc, char* argv[])
{
  CommandLine cmd;
  std::string egressMax = "100p";  // 出口 qdisc 大小

  std::string switchDelay = "0us"; // 交换机内部延迟，字符串格式（如 "0us", "200us", "2ms"）
  
  cmd.AddValue("EgressMax", "Egress QueueDisc MaxSize", egressMax);//need 优化

  cmd.AddValue("SwitchDelay", "Switch internal delay (e.g., '0us','200us','2ms')", switchDelay);
  bool traceSwitch = false;
  cmd.AddValue("TraceSwitch", "Enable switch delay timestamp logs", traceSwitch);

  cmd.Parse(argc, argv);

  LogComponentEnable("SwitchIngressEgressDemo", LOG_LEVEL_INFO);
  LogComponentEnable("BridgeNetDevice",          LOG_LEVEL_INFO);
  LogComponentEnable("CsmaNetDevice",            LOG_LEVEL_INFO);
  LogComponentEnable("QueueDisc",                LOG_LEVEL_INFO);

  NodeContainer hosts; hosts.Create(2); // A,B
  Ptr<Node> sw = CreateObject<Node>();  // Switch

  InternetStackHelper stack;
  stack.Install(hosts);
  stack.Install(sw);

  // ★ CSMA 链路的物理传播时延仍在这里设置（与“交换机内部延迟”不同）
  CsmaHelper csma;
  csma.SetChannelAttribute("DataRate", DataRateValue(DataRate("10Mbps")));
  csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2))); // 链路传播时延

  NetDeviceContainer aLink = csma.Install(NodeContainer(hosts.Get(0), sw));//主机，交换机
  NetDeviceContainer bLink = csma.Install(NodeContainer(hosts.Get(1), sw));

  NetDeviceContainer swPorts;
  swPorts.Add(aLink.Get(1));//接受A的端口
  swPorts.Add(bLink.Get(1));//接受B的端口

  // 安装“带内部延迟”的 Bridge
  DelayBridgeHelper bridgeHelper;
  NetDeviceContainer bridgeDev = bridgeHelper.Install(sw, swPorts);
  Ptr<DelayBridgeNetDevice> bridgeNd = DynamicCast<DelayBridgeNetDevice>(bridgeDev.Get(0));
  // 配置 SwitchDelay
  bridgeNd->SetAttribute("SwitchDelay", TimeValue(Time(switchDelay)));
  bridgeNd->SetAttribute("TraceSwitchDelay", BooleanValue(traceSwitch));


  // 出口方向安装自定义 Egress QueueDisc（可按需保留/删掉）
  TrafficControlHelper tch;
  tch.SetRootQueueDisc("ns3::SimpleEgressQueueDisc",
                      "MaxSize", StringValue(egressMax));
  QueueDiscContainer qdiscs = tch.Install(swPorts);
  for (uint32_t i = 0; i < qdiscs.GetN(); ++i)
  {
      qdiscs.Get(i)->TraceConnectWithoutContext("PacketsInQueue",
      MakeCallback(&EgressQTracer));
  }

  // ★ IP：两端主机在同一网段
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer ifA = ipv4.Assign(aLink.Get(0));
  Ipv4InterfaceContainer ifB = ipv4.Assign(bLink.Get(0));

  // A -> B：UDP 业务
  uint16_t port = 9000;
  UdpServerHelper server(port);
  auto sApp = server.Install(hosts.Get(1));
  sApp.Start(Seconds(0.5));
  sApp.Stop(Seconds(1.5));

  UdpClientHelper client(ifB.GetAddress(0), port);
  client.SetAttribute("MaxPackets", UintegerValue(1e9));
  client.SetAttribute("Interval", TimeValue(MilliSeconds(1)));
  client.SetAttribute("PacketSize", UintegerValue(1024));
  auto cApp = client.Install(hosts.Get(0));
  cApp.Start(Seconds(1));
  cApp.Stop(Seconds(1.5));

  Ptr<UdpServer> srv = DynamicCast<UdpServer>(sApp.Get(0));
  Simulator::Schedule(Seconds(11), [srv] {
      NS_LOG_UNCOND("UDP server received packets = " << srv->GetReceived());
  });

  Simulator::Stop(Seconds(11));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}

// ./ns3 run pause1 -- --SwitchDelay=2ms --TraceSwitch=1 --EgressMax=100p