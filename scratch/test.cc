#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/packet-socket-helper.h"
#include "ns3/mac48-address.h"
#include <vector>
#include "ns3/mobility-module.h"     // 用于设置节点位置
#include "ns3/point-to-point-module.h"
#include "ns3/mtp-module.h"

// 包含我们自定义的头文件
#include "ns3/hop-count-header.h"
#include "ns3/hop-count-client.h"
#include "ns3/min-hop-bridge-net-device.h"


NS_LOG_COMPONENT_DEFINE("FatTreeBridgeTopology");
using namespace ns3;

// 在全局变量之前添加
enum TrafficPattern {
    ONE_TO_ONE,
    ONE_TO_N,
    N_TO_N
};

// 修改全局变量：用结构体记录每个包的信息
struct PacketInfo {
    uint64_t uid;
    TrafficPattern pattern;
    Time txTime;
    Time rxTime;
    uint32_t srcNode;
    uint32_t dstNode; // 对于 1-to-n，可设为 ~0 或忽略

    // 新增：记录每一跳的时间（设备指针 → 接收和发送时间）
    std::vector<Time> road;

    PacketInfo(uint64_t u, TrafficPattern p, uint32_t s, uint32_t d)
        : uid(u), pattern(p), srcNode(s), dstNode(d) {
        txTime = NanoSeconds(0);
        rxTime = NanoSeconds(0);
    }
    PacketInfo() = default;
};

std::vector<PacketInfo> g_packetLog;  // 所有目标包的日志
std::map<uint64_t, PacketInfo> g_uidToInfo; // 快速查找

void
TxTraceCallback(std::string context, Ptr<const Packet> packet)//终端回调
{
    uint64_t uid = packet->GetUid();
    auto it = g_uidToInfo.find(uid);
    if (it != g_uidToInfo.end()) {
        PacketInfo& info = g_uidToInfo[uid];
        Time now1=Simulator::Now();
        if (info.txTime.IsZero()) {
            info.txTime = Simulator::Now();
            info.road.push_back(Simulator::Now());
            NS_LOG_INFO("[TX] UID=" << uid
                           << " | Pattern=" << info.pattern
                           << " | Src=" << info.srcNode << " Dst=" << info.dstNode
                           << " | Time=" << info.txTime.GetFemtoSeconds());
        }
    }
}
void
DeviceTxCallback(std::string context, Ptr<const Packet> packet)//交换机回调
{
    uint64_t uid = packet->GetUid();
    auto it = g_uidToInfo.find(uid);
    if (it != g_uidToInfo.end()) {
        PacketInfo& info = g_uidToInfo[uid];
        info.road.push_back(Simulator::Now());
        NS_LOG_INFO("[STX] UID=" << uid
                           << " sent via the switch" 
                           << " | Time=" << Simulator::Now().GetFemtoSeconds() << "s");
    }
}

void
RxTraceCallback(std::string context, Ptr<const Packet> packet)//终端回调
{
    uint64_t uid = packet->GetUid();
    auto it = g_uidToInfo.find(uid);
    if (it != g_uidToInfo.end()) {
        PacketInfo& info = g_uidToInfo[uid];
        Time now1=Simulator::Now();
        if (info.rxTime.IsZero()) {
            info.rxTime = Simulator::Now();
            info.road.push_back(Simulator::Now());
            NS_LOG_INFO("[RX] UID=" << uid
                           << " | Time=" << info.rxTime.GetFemtoSeconds() << "s");

            if (!info.txTime.IsZero()) {
                Time delay = info.rxTime - info.txTime;
                NS_LOG_INFO("[DELAY] UID=" << uid << " | Delay = " << delay.GetFemtoSeconds());
            }
        }
    }
}
void
DeviceRxCallback(std::string context, Ptr<const Packet> packet)//交换机回调
{
    uint64_t uid = packet->GetUid();
    auto it = g_uidToInfo.find(uid);
    if (it != g_uidToInfo.end()) {
        PacketInfo& info = g_uidToInfo[uid];
        info.road.push_back(Simulator::Now());
        NS_LOG_INFO("[SRX] UID=" << uid
                           << " Received via the switch" 
                           << " | Time=" << Simulator::Now().GetFemtoSeconds());
    }
}



void ScheduleOneToOne(uint32_t src, uint32_t dst, double startSeconds, int count,double waittime, NodeContainer hosts,uint32_t payloadSize) {
    Ptr<NetDevice> senderDev = hosts.Get(src)->GetDevice(0);
    Address srcAddr = senderDev->GetAddress();
    Address dstAddr = hosts.Get(dst)->GetDevice(0)->GetAddress();
    uint16_t proto = 0x88B5;

    for (int i = 0; i < count; ++i) {
        Ptr<Packet> p = Create<Packet>(payloadSize);
        HopCountHeader hopHeader;
        hopHeader.SetHopCount(0); // 初始跳数设为0
        hopHeader.SetIsFlood(false); 
        p->AddHeader(hopHeader);

        uint64_t uid = p->GetUid();
        double sendTime = startSeconds + i*waittime;

        PacketInfo info(uid, ONE_TO_ONE, src, dst);
        g_packetLog.push_back(info);
        g_uidToInfo[uid] = info;

        Simulator::Schedule(Seconds(sendTime), [senderDev, p, srcAddr, dstAddr, proto, uid]() {
            senderDev->SendFrom(p, srcAddr, dstAddr, proto);
        });
    }
}

void ScheduleOneToMany(uint32_t src, double startSeconds,NodeContainer hosts,uint32_t payloadSize) {
    uint32_t hostnum=hosts.GetN();
    Ptr<NetDevice> senderDev = hosts.Get(src)->GetDevice(0);
    Address srcAddr = senderDev->GetAddress();
    uint16_t proto = 0x88B5;

     // 从 startSeconds 开始，每个包间隔 1 飞秒
    Time sendTime = Seconds(startSeconds);  // 起始时间
    // Time interval = TimeStep(56680001);            // 1 飞秒 = 1 TimeStep (当分辨率设为 FS 时)
    Time interval = TimeStep(0); 

    for (uint32_t i=0;i<hostnum;i++) {
        if (i==src) continue;
        Ptr<Packet> p = Create<Packet>(payloadSize);
        HopCountHeader hopHeader;
        hopHeader.SetHopCount(0); // 初始跳数设为0
        hopHeader.SetIsFlood(false); 
        p->AddHeader(hopHeader);

        uint64_t uid = p->GetUid();

        PacketInfo info(uid, ONE_TO_N, src, i);
        g_packetLog.push_back(info);
        g_uidToInfo[uid] = info;
        Address dstAddr = hosts.Get(i)->GetDevice(0)->GetAddress();

        Simulator::Schedule(sendTime, [senderDev, p, srcAddr, dstAddr, proto, uid]() mutable {
            senderDev->SendFrom(p, srcAddr, dstAddr, proto);
        });
        sendTime += interval;
    }
}
void ScheduleAllToAll(double startSeconds,NodeContainer hosts,uint32_t payloadSize) {
    uint32_t n = hosts.GetN();
    uint16_t proto = 0x88B5;


    for (uint32_t i = 0; i < n; ++i) {
        Ptr<NetDevice> senderDev = hosts.Get(i)->GetDevice(0);
        Address srcAddr = senderDev->GetAddress();

        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) continue;

            Ptr<Packet> p = Create<Packet>(payloadSize);
            HopCountHeader hopHeader;
            hopHeader.SetHopCount(0); // 初始跳数设为0
            hopHeader.SetIsFlood(false); 
            p->AddHeader(hopHeader);

            uint64_t uid = p->GetUid();

            PacketInfo info(uid, N_TO_N, i, j);
            g_packetLog.push_back(info);
            g_uidToInfo[uid] = info;
            
            Address dstAddr = hosts.Get(j)->GetDevice(0)->GetAddress();
            Simulator::Schedule(Seconds(startSeconds), [senderDev, p, srcAddr, proto,dstAddr]() {
                senderDev->SendFrom(p, srcAddr, dstAddr, proto);
            });

        }
    }
}


int main(int argc, char *argv[]) {
    MtpInterface::Enable(4);
    Time::SetResolution(Time::FS);
    CommandLine cmd;
    cmd.Parse(argc, argv);

    // --- 配置参数 ---
    uint32_t k = 4;
    int max_jump=5;//最大学习跳数
    int ecmpmodel=1;//ECMP模式；0随机转发，1按包hash
    double simulationTime = 300.0;
    const uint32_t TARGET_PACKET_SIZE = 1024;//包大小（含头）


    // 计算节点数量


    uint32_t numHosts = k*k*k/4;//终端数量
    uint32_t numCores = (k/2)*(k/2);//核心层交换机数量
    uint32_t numAggregations = k*k/2;//汇聚层交换机数量
    uint32_t numEdges = k*k/2;//边缘层交换机数量

    // 启用日志
    LogComponentEnable("FatTreeBridgeTopology", LOG_LEVEL_INFO);
    // LogComponentEnable("MinHopBridgeNetDevice", LOG_LEVEL_LOGIC);
    // LogComponentEnable("HopCountClient", LOG_LEVEL_INFO);

    ///将日志输出重定向到文件
    // AsciiTraceHelper ascii;
    // Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream("simulation.log");
    // //重定向 C++ std::clog（ns-3 日志默认输出到 std::clog）
    // std::clog.rdbuf(stream->GetStream()->rdbuf());

    // --- 1. 创建所有节点 ---
    NS_LOG_INFO("创建 Fat-Tree 节点...");

    NodeContainer coreSwitches;
    coreSwitches.Create(numCores);

    NodeContainer aggregationSwitches;
    aggregationSwitches.Create(numAggregations);

    NodeContainer edgeSwitches;
    edgeSwitches.Create(numEdges);

    NodeContainer hosts;
    hosts.Create(numHosts);

    // --- 2. 为所有交换机创建 MinHopBridgeNetDevice ---
    NS_LOG_INFO("为所有交换机安装 MinHopBridgeNetDevice...");

    std::vector<Ptr<MinHopBridgeNetDevice>> coreDevices;
    for (uint32_t i = 0; i < coreSwitches.GetN(); ++i) {
        Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
        dev->set_maxjump(max_jump);
        dev->set_ecmp_model(ecmpmodel);
        coreSwitches.Get(i)->AddDevice(dev);
        coreDevices.push_back(dev);
    }

    std::vector<Ptr<MinHopBridgeNetDevice>> aggregationDevices;
    for (uint32_t i = 0; i < aggregationSwitches.GetN(); ++i) {
        Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
        dev->set_maxjump(max_jump);
        dev->set_ecmp_model(ecmpmodel);
        aggregationSwitches.Get(i)->AddDevice(dev);
        aggregationDevices.push_back(dev);
    }

    std::vector<Ptr<MinHopBridgeNetDevice>> edgeDevices;
    for (uint32_t i = 0; i < edgeSwitches.GetN(); ++i) {
        Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
        dev->set_maxjump(max_jump);
        dev->set_ecmp_model(ecmpmodel);
        edgeSwitches.Get(i)->AddDevice(dev);
        edgeDevices.push_back(dev);
    }

    // --- 3. 配置 CSMA 链路 ---
    PointToPointHelper p2pHelper;
    // csmaHelper.SetChannelAttribute("DataRate", StringValue("200Gbps"));
    p2pHelper.SetDeviceAttribute("DataRate", StringValue("200Gbps"));//链路带宽
    p2pHelper.SetChannelAttribute("Delay", StringValue("15ns"));
    p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1000p")); // 设置为 1000 个包
    p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(1500)); // 支持 30KB 帧

    // --- 4. 连接：核心层 ↔ 汇聚层 ---
    NS_LOG_INFO("连接核心层与汇聚层...");
    for (uint32_t i = 0; i < k / 2; ++i) { // 核心层第 i 行
        for (uint32_t j = 0; j < k / 2; ++j) { // 核心层第 j 列
            uint32_t coreIndex = i * (k / 2) + j;
            for (uint32_t p = 0; p < k; ++p) { // 每个 Pod
                uint32_t aggIndex = p * (k / 2) + i; // Pod p 的第 i 个 Aggregation Switch
                NodeContainer link(coreSwitches.Get(coreIndex), aggregationSwitches.Get(aggIndex));
                NetDeviceContainer devs = p2pHelper.Install(link);
                coreDevices[coreIndex]->AddBridgePort(devs.Get(0));
                aggregationDevices[aggIndex]->AddBridgePort(devs.Get(1));
            }
        }
    }

    // --- 5. 连接：汇聚层 ↔ 边缘层 ---
    NS_LOG_INFO("连接汇聚层与边缘层...");
    for (uint32_t p = 0; p < k; ++p) {
        for (uint32_t i = 0; i < k / 2; ++i) {
            for (uint32_t j = 0; j < k / 2; ++j) {
                uint32_t aggIndex = p * (k / 2) + i;
                uint32_t edgeIndex = p * (k / 2) + j;
                NodeContainer link(aggregationSwitches.Get(aggIndex), edgeSwitches.Get(edgeIndex));
                NetDeviceContainer devs = p2pHelper.Install(link);
                aggregationDevices[aggIndex]->AddBridgePort(devs.Get(0));
                edgeDevices[edgeIndex]->AddBridgePort(devs.Get(1));
            }
        }
    }

    // --- 6. 连接：边缘层 ↔ 主机 ---
    NS_LOG_INFO("连接边缘层与主机...");
    std::vector<Ptr<NetDevice>> hostDevices; // 保存主机设备用于后续应用
    for (uint32_t p = 0; p < k; ++p) {
        for (uint32_t i = 0; i < k / 2; ++i) {
            for (uint32_t j = 0; j < k / 2; ++j) {
                uint32_t edgeIndex = p * (k / 2) + i;
                uint32_t hostIndex = p * (k * k / 4) + i * (k / 2) + j;
                NodeContainer link(edgeSwitches.Get(edgeIndex), hosts.Get(hostIndex));
                NetDeviceContainer devs = p2pHelper.Install(link);
                edgeDevices[edgeIndex]->AddBridgePort(devs.Get(0));
                hostDevices.push_back(devs.Get(1)); // 主机侧设备
            }
        }
    }

    // --- 7. 获取所有主机的 MAC 地址 ---
    std::vector<Mac48Address> hostMacs;
    NS_LOG_INFO("--- 主机 MAC 地址列表 ---");
    for (uint32_t i = 0; i < hostDevices.size(); ++i) {
        Mac48Address mac = Mac48Address::ConvertFrom(hostDevices[i]->GetAddress());
        hostMacs.push_back(mac);
        NS_LOG_INFO("主机 " << i << " 的 MAC 地址: " << mac);
    }
    NS_LOG_INFO("--------------------------");

    
    // --- 8. 配置 PacketSocket 应用层 ---
    PacketSocketHelper packetSocketHelper;
    packetSocketHelper.Install(hosts); // 为所有主机安装 PacketSocket




    // 为每个主机安装发往其他主机的 HopCountClient
    uint16_t customEtherType = 0x88B5;
    for (uint32_t i = 0; i < numHosts; ++i){
        PacketSocketAddress clientSocketAddr;
        clientSocketAddr.SetSingleDevice(hostDevices[i]->GetIfIndex());
        clientSocketAddr.SetPhysicalAddress(hostMacs[numHosts-i]);
        clientSocketAddr.SetProtocol(customEtherType);

        Ptr<HopCountClient> client = CreateObject<HopCountClient>();
        client->SetRemote(clientSocketAddr);
        client->SetAttribute("PacketSize", UintegerValue(100));
        client->SetAttribute("Interval", TimeValue(Seconds(1.5)));
        client->SetAttribute("MaxPackets", UintegerValue(1));
        hosts.Get(i)->AddApplication(client);

        double startTime = 1.0 + (i * 0.2);
        ApplicationContainer clientApps(client);
        clientApps.Start(Seconds(startTime));
        clientApps.Stop(Seconds(simulationTime));
    }

    
    // --- 计算头部和 payload 大小 ---
    HopCountHeader hopHeader;
    hopHeader.SetHopCount(0);
    hopHeader.SetIsFlood(false);

    uint32_t headerSize = hopHeader.GetSerializedSize();
    NS_LOG_INFO("HopCountHeader size: " << headerSize << " bytes");

    
    uint32_t payloadSize = TARGET_PACKET_SIZE - headerSize;
    if (payloadSize < 0) {
        NS_FATAL_ERROR("Header too large!");
    }

    // --- 发送多种模式 ---
    NS_LOG_INFO("=== 开始发送多种模式数据包 ===");
    // 示例：1-to-1：主机0 → 主机1，发送2个包
    // double onetoonestart=100.0;
    // int count=3;
    // double waittime=1.0;
    // ScheduleOneToOne(0, numHosts-1, onetoonestart, count, waittime, hosts, payloadSize);

    // 示例：1-to-n：主机0 → 主机N
    double onetonstart=200.0;
    ScheduleOneToMany(0, onetonstart, hosts, payloadSize);

    // 示例：n-to-n：全网通信，从10.0s开始
    double ntonstart=200.0;
    // ScheduleAllToAll(ntonstart, hosts, payloadSize);


// 为每个主机的 NetDevice 绑定 Tx 和 Rx 跟踪
    for (uint32_t i = 0; i < hosts.GetN(); ++i) {
        Ptr<NetDevice> dev = hosts.Get(i)->GetDevice(0);

        // 连接 MacTx 跟踪源
        bool connected = dev->TraceConnect("MacTx", "MacTx", MakeCallback<void, std::string, Ptr<const Packet> >(&TxTraceCallback));
        if (!connected) {
            NS_LOG_ERROR("Failed to connect MacTx trace for host " << i);
        }   

        // 连接 MacRx 跟踪源
        connected = dev->TraceConnect("MacRx", "MacRx", MakeCallback<void, std::string, Ptr<const Packet>>(&RxTraceCallback));
        if (!connected) {
            NS_LOG_ERROR("Failed to connect MacRx trace for host " << i);
        }
    }
    for (uint32_t i = 0; i < edgeSwitches.GetN(); ++i) {
        for (uint32_t j=0;j<edgeSwitches.Get(i)->GetNDevices();j++){
            Ptr<NetDevice> dev = edgeSwitches.Get(i)->GetDevice(j);
            dev->TraceConnect("MacTx", "MacTx", MakeCallback<void, std::string, Ptr<const Packet> >(&DeviceTxCallback));
            dev->TraceConnect("MacRx", "MacRx", MakeCallback<void, std::string, Ptr<const Packet>>(&DeviceRxCallback));
        }
    }
    for (uint32_t i = 0; i < aggregationSwitches.GetN(); ++i) {
        for (uint32_t j=0;j<aggregationSwitches.Get(i)->GetNDevices();j++){
            Ptr<NetDevice> dev = aggregationSwitches.Get(i)->GetDevice(j);
            dev->TraceConnect("MacTx", "MacTx", MakeCallback<void, std::string, Ptr<const Packet> >(&DeviceTxCallback));
            dev->TraceConnect("MacRx", "MacRx", MakeCallback<void, std::string, Ptr<const Packet>>(&DeviceRxCallback));
        }
    }
    for (uint32_t i = 0; i < coreSwitches.GetN(); ++i) {
        for (uint32_t j=0;j<coreSwitches.Get(i)->GetNDevices();j++){
            Ptr<NetDevice> dev = coreSwitches.Get(i)->GetDevice(j);
            dev->TraceConnect("MacTx", "MacTx", MakeCallback<void, std::string, Ptr<const Packet> >(&DeviceTxCallback));
            dev->TraceConnect("MacRx", "MacRx", MakeCallback<void, std::string, Ptr<const Packet>>(&DeviceRxCallback));
        }
    }
    

    // --- 9. 运行仿真 ---
    NS_LOG_INFO("开始仿真，持续时间: " << simulationTime << " 秒...");
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // NS_LOG_INFO("--- 打印所有 MinHopBridgeNetDevice 的学习状态 ---");
    // for (auto dev : coreDevices) dev->PrintLearnedState();
    // for (auto dev : aggregationDevices) dev->PrintLearnedState();
    // for (auto dev : edgeDevices) dev->PrintLearnedState();
    // NS_LOG_INFO("--------------------------------------------------");


    NS_LOG_INFO("--- Final Traffic Pattern Summary ---");

// 按模式分组统计
    Time oneToOneDelayTotal = NanoSeconds(0);
    int oneToOneCount = 0;

    Time oneToNmax=NanoSeconds(0);
    Time NToNmax=NanoSeconds(0);

    for (auto inf : g_uidToInfo) {
        PacketInfo info =inf.second;
        if (info.pattern == ONE_TO_ONE) {
            if (!info.txTime.IsZero() && !info.rxTime.IsZero()) {
                Time delay = info.rxTime - info.txTime;
                oneToOneDelayTotal += delay;
                oneToOneCount++;
                NS_LOG_INFO("1-to-1 UID=" << info.uid << " | Delay=" << delay.As(Time::S));
                for (Time mtime : info.road){
                    NS_LOG_INFO(mtime.GetFemtoSeconds());
                }
            }
        }
        else if (info.pattern == ONE_TO_N) {
            if (!info.rxTime.IsZero()) {
                if (info.rxTime>oneToNmax) oneToNmax=info.rxTime;
            }
        }
        else if (info.pattern == N_TO_N) {
            if (!info.rxTime.IsZero()) {
                if (info.rxTime>NToNmax) NToNmax=info.rxTime;
            }
        }
    }

// 输出统计
    if (oneToOneCount > 0) {
        NS_LOG_INFO("==> 1-to-1 Average E2E Delay: " 
                << (oneToOneDelayTotal / oneToOneCount).As(Time::S));
    }

    if (oneToNmax!=NanoSeconds(0)){
        NS_LOG_INFO("==> 1-to-N  | Completion Time = " << oneToNmax.GetSeconds()-onetonstart);
    }
    if (NToNmax!=NanoSeconds(0)){
        NS_LOG_INFO("==> N-to-N  | Completion Time = " << NToNmax.GetSeconds()-ntonstart);
    }



    Simulator::Destroy();
    NS_LOG_INFO("仿真结束。");

    return 0;
}