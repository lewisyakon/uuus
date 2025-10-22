#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/packet-socket-helper.h"
#include "ns3/mac48-address.h"
#include <vector>
#include "ns3/mobility-module.h"     // 用于设置节点位置
#include "ns3/point-to-point-module.h"
#include "ns3/mpi-module.h"

// 包含我们自定义的头文件
#include "ns3/hop-count-header.h"
#include "ns3/hop-count-client.h"
#include "ns3/min-hop-bridge-net-device.h"
#include "ns3/three-phase-protocol.h"
#include "ns3/mtp-module.h"

#include <mutex>
#include <iostream>

#define LOCAL(r) ((r) == conf::rank)
#define LOG(content)                                                                               \
    {                                                                                              \
        if (conf::rank == 0)                                                                       \
            std::cout << content << "\n";                                                          \
    }

NS_LOG_COMPONENT_DEFINE ("FatTreeBridgeTopology");

using namespace ns3;

namespace conf
{
uint32_t system = 0;
uint32_t rank = 0;
bool nullmsg = false;
}; 

void Initialize(int argc, char * argv[])
{
    CommandLine cmd;
    // parse mtp/mpi options
    cmd.AddValue("system", "Number of logical processes in MTP manual partition", conf::system);
    cmd.AddValue("nullmsg", "Enable null message algorithm", conf::nullmsg);
    cmd.Parse(argc, argv);
    // initialize mpi
    if (conf::nullmsg)
    {
        GlobalValue::Bind("SimulatorImplementationType",
                          StringValue("ns3::NullMessageSimulatorImpl"));
    }
    else
    {
        GlobalValue::Bind("SimulatorImplementationType",
                          StringValue("ns3::DistributedSimulatorImpl"));
    }
    MpiInterface::Enable(&argc, &argv);
    conf::rank = MpiInterface::GetSystemId();
    conf::system = MpiInterface::GetSize();
}

double simulationTime = 300.0;
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
std::vector<Time> g_localRxTimeList;
std::map<uint64_t, PacketInfo> g_uidToInfo; // 快速查找
std::mutex g_logMutex;


void
TxTraceCallback(Ptr<const Packet> packet)//终端回调
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    uint64_t uid = packet->GetUid();
    Ptr<Packet> p = packet->Copy();
    PppHeader ppp;
    p->RemoveHeader(ppp);
    HopCountHeader hopHeader;
    p->RemoveHeader(hopHeader);

    ThreePHeader TPheader;
    p->RemoveHeader(TPheader);

    if (!hopHeader.IsFlood()){
        PacketInfo info(uid, ONE_TO_ONE, 0, 0);
        g_packetLog.push_back(info);
        g_uidToInfo[uid] = info;
    }
    
    auto it = g_uidToInfo.find(uid);
    if (it != g_uidToInfo.end()) {
        PacketInfo& info = g_uidToInfo[uid];
        Time now1=Simulator::Now();
        if (info.txTime.IsZero()) {
            info.txTime = Simulator::Now();
            info.road.push_back(Simulator::Now());
            // LOG("[TX] UID=" << uid
            //                << " | Pattern=" << info.pattern
            //                << " | Src=" << info.srcNode << " Dst=" << info.dstNode
            //                << " | Time=" << info.txTime.GetFemtoSeconds());
        }
    }
}
void
DeviceTxCallback(std::string context, Ptr<const Packet> packet)//交换机回调
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    uint64_t uid = packet->GetUid();
    auto it = g_uidToInfo.find(uid);
    if (it != g_uidToInfo.end()) {
        PacketInfo& info = g_uidToInfo[uid];
        info.road.push_back(Simulator::Now());
        // LOG("[STX] UID=" << uid
        //                    << " sent via the switch" 
        //                    << " | Time=" << Simulator::Now().GetFemtoSeconds() << "s");
    }
}

void
RxTraceCallback(std::string context, Ptr<const Packet> packet)//终端回调
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    Time rxTime = Simulator::Now();
    g_localRxTimeList.push_back(rxTime);
    // uint64_t uid = packet->GetUid();
    // auto it = g_uidToInfo.find(uid);
    // if (it != g_uidToInfo.end()) {
    //     PacketInfo& info = g_uidToInfo[uid];
    //     Time now1=Simulator::Now();
    //     if (info.rxTime.IsZero()) {
    //         info.rxTime = Simulator::Now();
    //         info.road.push_back(Simulator::Now());
    //         // LOG("[RX] UID=" << uid
    //         //                << " | Time=" << info.rxTime.GetFemtoSeconds() << "s");

    //         if (!info.txTime.IsZero()) {
    //             // Time delay = info.rxTime - info.txTime;
    //             // NS_LOG_INFO("[DELAY] UID=" << uid << " | Delay = " << delay.GetFemtoSeconds());
    //         }
    //     }
    // }
}
void
DeviceRxCallback(std::string context, Ptr<const Packet> packet)//交换机回调
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    uint64_t uid = packet->GetUid();
    auto it = g_uidToInfo.find(uid);
    if (it != g_uidToInfo.end()) {
        PacketInfo& info = g_uidToInfo[uid];
        info.road.push_back(Simulator::Now());
        // LOG("[SRX] UID=" << uid
        //                    << " Received via the switch" 
        //                    << " | Time=" << Simulator::Now().GetFemtoSeconds());
    }
}



void ScheduleOneToOne(uint32_t src, uint32_t dst, double startSeconds, int count,double waittime, NodeContainer hosts,uint32_t payloadSize) {
    double begintime=startSeconds;
    for (int i = 0; i < count; ++i) {
        // 服务端应用（节点dst）
        Ptr<RequestorApp> App = CreateObject<RequestorApp>();
        App->Setup(uint32_t(0),0x9999);
        App->setdatalen(payloadSize);
        hosts.Get(dst)->AddApplication(App);
        App->initialization();
        App->SetStartTime(Seconds(begintime));
        App->SetStopTime(Seconds(simulationTime));

        PacketSocketAddress clientSocketAddr;
        clientSocketAddr.SetSingleDevice(uint32_t(0));
        clientSocketAddr.SetPhysicalAddress(hosts.Get(dst)->GetDevice(0)->GetAddress());//往服务器发
        clientSocketAddr.SetProtocol(0x9999);
        

        App = CreateObject<RequestorApp>();
        App->Setup(uint32_t(0),0x9999);
        App->setdatalen(payloadSize);
        hosts.Get(src)->AddApplication(App);
        App->initialization();
        App->SetStartTime(Seconds(begintime));
        App->SetStopTime(Seconds(simulationTime));
        App->AddServer(clientSocketAddr);

        App->TriggerSendTo(0,Seconds(begintime));
        begintime+=waittime;
    }
}

void ScheduleOneToMany(uint32_t src, double startSeconds,NodeContainer hosts,uint32_t payloadSize) {
    uint32_t hostnum=hosts.GetN();
     // 从 startSeconds 开始，每个包间隔 1 飞秒
    Time sendTime = Seconds(startSeconds);  // 起始时间
    // Time interval = TimeStep(56680001);            // 1 飞秒 = 1 TimeStep (当分辨率设为 FS 时)
    Time interval = TimeStep(0); 
    uint32_t pro=0X9999;
    std::vector<Ptr<RequestorApp> > Apps;
    for (uint32_t i=0;i<hostnum;i++) {
        uint16_t proto=pro;
        uint32_t dst=i;
        // 服务端应用（节点dst）
        Ptr<RequestorApp> App = CreateObject<RequestorApp>();
        Apps.push_back(App);
        App->Setup(uint32_t(0),proto);
        App->setdatalen(payloadSize);
        hosts.Get(dst)->AddApplication(App);
        App->SetStartTime(sendTime);
        App->SetStopTime(Seconds(simulationTime));
        App->initialization();

        sendTime += interval;
    }
    for (uint32_t i=0;i<hostnum;i++){
        Ptr<RequestorApp> App=Apps[src];
        if (i!=src){
            PacketSocketAddress clientSocketAddr;
            clientSocketAddr.SetSingleDevice(uint32_t(0));
            clientSocketAddr.SetPhysicalAddress(hosts.Get(i)->GetDevice(0)->GetAddress());//往服务器发
            clientSocketAddr.SetProtocol(pro);
            App->AddServer(clientSocketAddr);
        }
    }
    Ptr<RequestorApp> App = Apps[src];
    int j=0;
    for (uint32_t i=0;i<hostnum;i++) {
        if (i==src) continue;
        App->TriggerSendTo(j,sendTime);
        j++;
    }
}
void ScheduleAllToAll(double startSeconds,NodeContainer hosts,uint32_t payloadSize) {
    uint32_t n = hosts.GetN();
    uint32_t pro=0X9999;
    Time sendTime = Seconds(startSeconds);  // 起始时间
    std::vector<Ptr<RequestorApp> > Apps(n);
    for (uint32_t i = 0; i < n; ++i) {
        Ptr<Node> clientNode = hosts.Get(i);

        if(LOCAL(clientNode->GetSystemId()))
        {
            uint16_t proto=pro;
            Ptr<RequestorApp> App = CreateObject<RequestorApp>();
            Apps[i] = App;
            App->Setup(uint32_t(0),proto);
            App->setdatalen(payloadSize);
            hosts.Get(i)->AddApplication(App);
            App->initialization();
            App->SetStartTime(sendTime);
            App->SetStopTime(Seconds(simulationTime));
            for (uint32_t j = 0; j < n; ++j) {
                if (i == j) continue;
                // 服务端应用（节点dst）
                uint32_t dst=j;

                PacketSocketAddress clientSocketAddr;
                clientSocketAddr.SetSingleDevice(uint32_t(0));
                clientSocketAddr.SetPhysicalAddress(hosts.Get(dst)->GetDevice(0)->GetAddress());//往服务器发
                clientSocketAddr.SetProtocol(proto);

                App->AddServer(clientSocketAddr);
            }            
        }

    }
    for (uint32_t i = 0; i < n; ++i){
        Ptr<Node> clientNode = hosts.Get(i);

        if(LOCAL(clientNode->GetSystemId()))
        {
            int k=0;
            Ptr<RequestorApp> App = Apps[i];
            for (uint32_t j = 0; j < n; ++j){
                if (i == j) continue;
                App->TriggerSendTo(k,sendTime);
                k++;
            }            
        }
    }
}

void PartitionNodes(uint32_t k,
                    uint32_t numCores,
                    uint32_t numAggregations,
                    uint32_t numEdges,
                    uint32_t numHosts,
                    NodeContainer& coreSwitches,
                    NodeContainer& aggregationSwitches,
                    NodeContainer& edgeSwitches,
                    NodeContainer& hosts)
{
    uint32_t numRanks = conf::system;
    if (numRanks == 1) return; // 单进程不需要分区

    // 1. Pods 内节点分区 (主机、边缘层、汇聚层) - 块分配 (Block Partitioning)

    for (uint32_t p = 0; p < k; ++p) { // 遍历所有 Pods (0 到 k-1)
        // 计算当前 Pod 应该分配给哪个 Rank (SystemId)
        uint32_t targetRank = p % numRanks;
        
        // 1.1 汇聚层交换机 (Aggregation Switches)
        for (uint32_t i = 0; i < k / 2; ++i) { 
            uint32_t aggIndex = p * (k / 2) + i;
            aggregationSwitches.Get(aggIndex)->SetSystemId(targetRank);
        }
        
        // 1.2 边缘层交换机 (Edge Switches)
        for (uint32_t i = 0; i < k / 2; ++i) { 
            uint32_t edgeIndex = p * (k / 2) + i;
            edgeSwitches.Get(edgeIndex)->SetSystemId(targetRank);
        }
        
        // 1.3 主机 (Hosts)
        for (uint32_t i = 0; i < k * k / 4; ++i) { 
            uint32_t hostIndex = p * (k * k / 4) + i;
            hosts.Get(hostIndex)->SetSystemId(targetRank);
        }
    }

    // 2. 核心层交换机分区 (Core Switches) - 轮询分配 (Round-Robin)
    for (uint32_t i = 0; i < numCores; ++i) {
        uint32_t targetRank = i % numRanks; // 核心交换机按索引轮询分配
        coreSwitches.Get(i)->SetSystemId(targetRank);
    }
}

int main(int argc, char *argv[]) {
    // MtpInterface::Enable(4);
    Initialize(argc, argv);

    Time::SetResolution(Time::FS);
    CommandLine cmd;
    cmd.Parse(argc, argv);

    // --- 配置参数 ---
    uint32_t k = 8;
    int max_jump=5;//最大学习跳数
    int ecmpmodel=1;//ECMP模式；0随机转发，1按包hash

    simulationTime = 300.0;
    const uint32_t TARGET_PACKET_SIZE = 27865;//包大小（含头）
    int sendmode=2;//发送模式。0表1对1；1表1对N；2表N对N

    double onetoonestart=200.0;
    double onetonstart=200.0;
    double ntonstart=200.0;


    // 计算节点数量


    uint32_t numHosts = k*k*k/4;//终端数量
    uint32_t numCores = (k/2)*(k/2);//核心层交换机数量
    uint32_t numAggregations = k*k/2;//汇聚层交换机数量
    uint32_t numEdges = k*k/2;//边缘层交换机数量

    // 启用日志
    // LogComponentEnable("FatTreeBridgeTopology",LOG_LEVEL_INFO);
    // LogComponentEnable("MultithreadedSimulatorImpl", LOG_LEVEL_INFO);
    // LogComponentEnable("MtpInterface",LOG_LEVEL_INFO);
    // LogComponentEnable("LogicalProcess", LOG_LEVEL_INFO);
    // LogComponentEnable("MinHopBridgeNetDevice", LOG_LEVEL_INFO);
    // LogComponentEnable("HopCountClient", LOG_LEVEL_INFO);
    // LogComponentEnable("ThreePhaseProtocol", LOG_LEVEL_INFO);
    LogComponentEnable("DistributedSimulatorImpl",LOG_LEVEL_INFO);

    // //将日志输出重定向到文件
    // AsciiTraceHelper ascii;
    // Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream("simulation.log");
    // //重定向 C++ std::clog（ns-3 日志默认输出到 std::clog）
    // std::clog.rdbuf(stream->GetStream()->rdbuf());

    // --- 1. 创建所有节点 ---
    // NS_LOG_INFO("创建 Fat-Tree 节点...");

    NodeContainer coreSwitches;
    coreSwitches.Create(numCores);

    NodeContainer aggregationSwitches;
    aggregationSwitches.Create(numAggregations);

    NodeContainer edgeSwitches;
    edgeSwitches.Create(numEdges);

    NodeContainer hosts;
    hosts.Create(numHosts);

    PartitionNodes(k, numCores, numAggregations, numEdges, numHosts,
                   coreSwitches, aggregationSwitches, edgeSwitches, hosts);

    // --- 2. 为所有交换机创建 MinHopBridgeNetDevice ---
    // NS_LOG_INFO("为所有交换机安装 MinHopBridgeNetDevice...");

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

    // --- 3. 配置 P2P 链路 ---
    PointToPointHelper p2pHelper;
    p2pHelper.SetDeviceAttribute("DataRate", StringValue("200Gbps"));//链路带宽
    p2pHelper.SetChannelAttribute("Delay", StringValue("15ns"));
    p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100000p")); 
    p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(1500)); // 支持 30KB 帧


    // --- 4. 连接：核心层 ↔ 汇聚层 ---
    // NS_LOG_INFO("连接核心层与汇聚层...");
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
    // NS_LOG_INFO("连接汇聚层与边缘层...");
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
    // NS_LOG_INFO("连接边缘层与主机...");
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
    // NS_LOG_INFO("--- 主机 MAC 地址列表 ---");
    for (uint32_t i = 0; i < hostDevices.size(); ++i) {
        Mac48Address mac = Mac48Address::ConvertFrom(hostDevices[i]->GetAddress());
        hostMacs.push_back(mac);
        // NS_LOG_INFO("主机 " << i << " 的 MAC 地址: " << mac);
    }
    // NS_LOG_INFO("--------------------------");

    
    // --- 8. 配置 PacketSocket 应用层 ---
    PacketSocketHelper packetSocketHelper;
    packetSocketHelper.Install(hosts); // 为所有主机安装 PacketSocket




    // 为每个主机安装发往其他主机的 HopCountClient
    uint16_t customEtherType = 0x88B5;
    // NS_LOG_INFO("为所有主机安装 HopCountClient 实现全网通信...");
    for (uint32_t i = 0; i < numHosts; ++i){
        Ptr<Node> clientNode = hosts.Get(i);
        if(LOCAL(clientNode->GetSystemId()))
        {
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
    }

    
    // --- 计算头部和 payload 大小 ---
    HopCountHeader hopHeader;
    hopHeader.SetHopCount(0);
    hopHeader.SetIsFlood(false);

    uint32_t headerSize = hopHeader.GetSerializedSize();
    // NS_LOG_INFO("HopCountHeader size: " << headerSize << " bytes");

    
    uint32_t payloadSize = TARGET_PACKET_SIZE - headerSize;
    if (payloadSize < 0) {
        NS_FATAL_ERROR("Header too large!");
    }

    // --- 发送多种模式 ---
    LOG("=== 开始发送多种模式数据包 ===");
   
    if (sendmode==0){
        //示例：1-to-1：主机0 → 主机1，发送2个包
        int count=1;
        double waittime=1.0;
        ScheduleOneToOne(0, numHosts-1, onetoonestart, count, waittime, hosts, payloadSize);
    }
    else if (sendmode==1){
        // 示例：1-to-n：主机0 → 主机N
        ScheduleOneToMany(0, onetonstart, hosts, payloadSize);
    }
    else if (sendmode==2){
        // 示例：n-to-n：全网通信，从10.0s开始
        ScheduleAllToAll(ntonstart, hosts, payloadSize);
    }
    

//可视化配置-----------------------------------------------------------------------------------------------------------------------
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

    double xStep = 10.0;   // 同之间的水平间距
    double yStep = 15.0;   // 不同层内节点垂直间距
    double xOffset = 0.0;  //额外水平偏移

    //核心层交换机位置
    xOffset = xStep * k/4;
    for (uint32_t i = 0; i < numCores; i++) positionAlloc->Add(Vector(i * (xStep*k)+xOffset, 0.0, 0.0));
    //汇聚层
    for (uint32_t i = 0; i < numAggregations; i++) positionAlloc->Add(Vector(i * (xStep*(k/2)), yStep, 0.0));
    //边缘层
    for (uint32_t i = 0; i < numEdges; i++) positionAlloc->Add(Vector(i * (xStep*(k/2)), yStep*2, 0.0));
    //主机（服务器）
    for (uint32_t i = 0; i < numHosts; i++) positionAlloc->Add(Vector(i * xStep, yStep*3, 0.0));
    
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");//强制固定位置
    mobility.Install(coreSwitches);
    mobility.Install(aggregationSwitches);
    mobility.Install(edgeSwitches);
    mobility.Install(hosts);


// 为每个主机的 NetDevice 绑定 Tx 和 Rx 跟踪
    for (uint32_t i = 0; i < hosts.GetN(); ++i) {
        Ptr<NetDevice> dev = hosts.Get(i)->GetDevice(0);

        // 连接 MacTx 跟踪源
        bool connected = dev->TraceConnectWithoutContext("MacTx", MakeCallback(&TxTraceCallback));
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
    LOG("开始仿真，持续时间: " << simulationTime << " 秒...");
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // NS_LOG_INFO("--- 打印所有 MinHopBridgeNetDevice 的学习状态 ---");
    // for (auto dev : coreDevices) dev->PrintLearnedState();
    // for (auto dev : aggregationDevices) dev->PrintLearnedState();
    // for (auto dev : edgeDevices) dev->PrintLearnedState();
    // NS_LOG_INFO("--------------------------------------------------");


    LOG("--- Final Traffic Pattern Summary ---");

// 统计

    Time timemax=NanoSeconds(0);
    // for (auto inf : g_uidToInfo) {
    //     PacketInfo info =inf.second;
    //     if (!info.rxTime.IsZero()) {
    //         if (info.rxTime>timemax) timemax=info.rxTime;
    //     }
    // }
    for(const auto&rxtime : g_localRxTimeList)
    {
        if(rxtime > timemax)
        {
            timemax = rxtime;
        }
    }
    long sendbuf = timemax.GetInteger();
    long recvbuf;
    // 使用 MPI_MAX 操作，找到所有 Rank 中最大的接收时间
    MPI_Allreduce(&sendbuf, &recvbuf, 1, MPI_LONG, MPI_MAX, MpiInterface::GetCommunicator());

    Time maxGlobalRxTime = TimeStep(recvbuf);

    
// 输出统计
    if (sendmode==0) {
        LOG("==> 1-to-1  | Completion Time = " << maxGlobalRxTime.GetSeconds()-onetoonestart);
    }

    if (sendmode==1){
        LOG("==> 1-to-N  | Completion Time = " << maxGlobalRxTime.GetSeconds()-onetonstart);
    }
    if (sendmode==2){
        LOG("==> N-to-N  | Completion Time = " << maxGlobalRxTime.GetSeconds()-ntonstart);
    }

    // //核心层交换机
    // for (uint32_t i = 0; i < numCores; i++) NS_LOG_INFO("core nodeId : " << coreSwitches.Get(i)->GetId() << " systemId: " << coreSwitches.Get(i)->GetSystemId());
    // //汇聚层
    // for (uint32_t i = 0; i < numAggregations; i++) NS_LOG_INFO("agg nodeId : " << aggregationSwitches.Get(i)->GetId() << " systemId: " << aggregationSwitches.Get(i)->GetSystemId());
    // //边缘层
    // for (uint32_t i = 0; i < numEdges; i++) NS_LOG_INFO("edge nodeId : " << edgeSwitches.Get(i)->GetId() << " systemId: " << edgeSwitches.Get(i)->GetSystemId());
    // //主机（服务器）
    // for (uint32_t i = 0; i < numHosts; i++) NS_LOG_INFO("host nodeId : " << hosts.Get(i)->GetId() << " systemId: " << hosts.Get(i)->GetSystemId());

    Simulator::Destroy();
    LOG("仿真结束。");

    MpiInterface::Disable();
    return 0;
}