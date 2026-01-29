/* 
*Copyright (c) 2024, Alibaba Group;
*Licensed under the Apache License, Version 2.0 (the "License");
*you may not use this file except in compliance with the License.
*You may obtain a copy of the License at

*   http://www.apache.org/licenses/LICENSE-2.0

*Unless required by applicable law or agreed to in writing, software
*distributed under the License is distributed on an "AS IS" BASIS,
*WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*See the License for the specific language governing permissions and
*limitations under the License.
*/

#ifndef __COMMON_H__
#define __COMMON_H__

#undef PGO_TRAINING
#define PATH_TO_PGO_CONFIG "path_to_pgo_config"

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/packet-socket-helper.h"
#include "ns3/mac48-address.h"
#include <vector>
#include "ns3/mobility-module.h"     // 用于设置节点位置
#include "ns3/point-to-point-module.h"

// 包含我们自定义的头文件
#include "ns3/hop-count-header.h"
#include "ns3/hop-count-client.h"
#include "ns3/min-hop-bridge-net-device.h"
#include "ns3/three-phase-protocol.h"
#include "ns3/mtp-module.h"
#include "ns3/my_sendflowclient.h"
#ifdef NS3_MPI
#include "ns3/mpi-interface.h"
#endif

#include <mutex>
#include <map>
#include <nlohmann/json.hpp>

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("GENERIC_SIMULATION");

GPUType gpu_type;
int ecmpmodel,max_jump,floadtime,m_mtu;
uint32_t node_num, switch_num, link_num, trace_num, nvswitch_num, gpus_per_server,
            nvswitch_per_server,numGPUs;
uint32_t Topology_model,subnet_model,subnet_k,numHosts,switch_delay;
string normallink_rate,nvlink_rate,normallink_delay,nvlink_delay,DropTailQueue_size,DropTailQueue_num;
uint32_t k_fattree,numCores,numAggregations,numEdges,k_all;
uint32_t k_clos,numSpine,numLeaf,hostperleaf;
uint32_t k_torus,torus_x,torus_y,num_torusswitch;
uint32_t k_mesh,num_meshswitch;
uint32_t k_test,num_testswitch;
uint32_t m_thread;
string network_workload;

std::vector<int>NVswitchs;

std::map<int,PacketSocketAddress> nodeaddresstable;
std::map<int,Ptr<sendflowclient> > nodeapptable;
std::map<int,Ptr<Node> > nodetable;

std::vector<Ptr<MinHopBridgeNetDevice>> coreDevices;
std::vector<Ptr<MinHopBridgeNetDevice>> aggregationDevices; 
std::vector<Ptr<MinHopBridgeNetDevice>> edgeDevices;
std::vector<Ptr<MinHopBridgeNetDevice>> nvswitchDevices;

std::vector<Ptr<MinHopBridgeNetDevice>> SpineDevices;
std::vector<Ptr<MinHopBridgeNetDevice>> LeafDevices;

std::vector<Ptr<MinHopBridgeNetDevice>> TorusDevices;

std::vector<Ptr<MinHopBridgeNetDevice>> FullmeshDevices;

std::vector<Ptr<MinHopBridgeNetDevice>> TestSwitchDevices;

std::vector<Ptr<MinHopBridgeNetDevice>> SubNetDevices;

static inline Ptr<MinHopBridgeNetDevice>
CreateGpuSubnetBridge(Ptr<Node> gpu, uint32_t intraMaxJump)
{
    Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
    dev->set_maxjump(intraMaxJump);
    dev->set_ecmp_model(ecmpmodel);
    dev->Set_switchDelay(NanoSeconds(0));
    dev->SetMtu(m_mtu);
    gpu->AddDevice(dev);
    return dev;
}
static inline Mac48Address
GetBridgeMac(const Ptr<MinHopBridgeNetDevice>& bridge)
{
    return Mac48Address::ConvertFrom(bridge->GetAddress());
}
static inline void
SetNetDeviceMac(const Ptr<NetDevice>& dev, const Mac48Address& mac)
{
    dev->SetAddress(mac);
}

static inline void
NormalizeGpuMacsToBridge(Ptr<Node> gpu, const Ptr<MinHopBridgeNetDevice>& bridge)
{
    const Mac48Address mac = GetBridgeMac(bridge);
    for (uint32_t di = 1; di < gpu->GetNDevices(); ++di) {
        gpu->GetDevice(di)->SetAddress(mac);
    }
}
static inline void
BuildServerSubnet_NvSwitch(uint32_t serverId, NodeContainer& hosts, NodeContainer& nvswitch,
                           PointToPointHelper& nvlinkhelper)
{
    //计算偏移量，否则因为重新编号的原因会把第一个子网反复构建，后面啥都没有
    const uint32_t baseGpu = serverId * gpus_per_server;
    for (uint32_t k = 0; k < nvswitch_per_server; ++k) {
        const uint32_t swIdx = serverId * nvswitch_per_server + k;
        Ptr<Node> swNode = nvswitch.Get(swIdx);
        Ptr<MinHopBridgeNetDevice> swBridge = nvswitchDevices.at(swIdx);

        for (uint32_t g = 0; g < gpus_per_server; ++g) {
            const uint32_t gpuIdx = baseGpu + g;
            Ptr<Node> gpuNode = hosts.Get(gpuIdx);
            Ptr<MinHopBridgeNetDevice> gpuBridge = SubNetDevices.at(gpuIdx);
            NS_ASSERT_MSG(gpuBridge != nullptr, "GPU subnet bridge not initialized");

            NodeContainer link(swNode, gpuNode);
            NetDeviceContainer devs = nvlinkhelper.Install(link);

            swBridge->AddBridgePort(devs.Get(0));
            gpuBridge->AddBridgePort(devs.Get(1));
        }
    }
    for (uint32_t g=0;g<gpus_per_server;g++){
        uint32_t gpuindex=serverId*gpus_per_server+g;
        NormalizeGpuMacsToBridge(hosts.Get(gpuindex),SubNetDevices[gpuindex]);
    }
}
static inline void
BuildServerSubnet_FullMesh(uint32_t serverId, NodeContainer& hosts,
                           PointToPointHelper& nvlinkhelper)
{
    const uint32_t baseGpu = serverId * gpus_per_server;//计算偏移量
    for (uint32_t i=0;i<gpus_per_server-1;i++){
        for (uint32_t j=i+1;j<gpus_per_server;j++){
            if (i==j) continue;
            NodeContainer link(hosts.Get(i+baseGpu), hosts.Get(j+baseGpu));
            NetDeviceContainer devs = nvlinkhelper.Install(link);
            SubNetDevices[i+baseGpu]->AddBridgePort(devs.Get(0));
            SubNetDevices[j+baseGpu]->AddBridgePort(devs.Get(1));
        }
    }
    //统一节点地址防止过长的mac表出现
    for (uint32_t i=0;i<gpus_per_server;i++){
        uint32_t gpuindex=serverId*gpus_per_server+i;
        NormalizeGpuMacsToBridge(hosts.Get(gpuindex),SubNetDevices[gpuindex]);
    }
}
static inline void
BuildServerSubnet_Torus(uint32_t serverId, NodeContainer& hosts,
                           PointToPointHelper& nvlinkhelper)
{
    const uint32_t baseGpu = serverId * gpus_per_server;//计算偏移量
    for (uint32_t x=0;x<subnet_k;x++){
        for (uint32_t y=0;y<subnet_k;y++){
            uint32_t idnow=x*subnet_k+y+baseGpu;
            uint32_t id_right=x*subnet_k+((y+1)%subnet_k)+baseGpu;
            uint32_t id_down=((x+1)%subnet_k)*subnet_k+y+baseGpu;

            NodeContainer link_x(hosts.Get(idnow), hosts.Get(id_right));
            NetDeviceContainer devs_x = nvlinkhelper.Install(link_x);
            SubNetDevices[idnow]->AddBridgePort(devs_x.Get(0));
            SubNetDevices[id_right]->AddBridgePort(devs_x.Get(1));

            NodeContainer link_y(hosts.Get(idnow), hosts.Get(id_down));
            NetDeviceContainer devs_y = nvlinkhelper.Install(link_y);
            SubNetDevices[idnow]->AddBridgePort(devs_y.Get(0));
            SubNetDevices[id_down]->AddBridgePort(devs_y.Get(1));
        }
    }
    //统一节点地址防止过长的mac表出现
    for (uint32_t i=0;i<gpus_per_server;i++){
        uint32_t gpuindex=serverId*gpus_per_server+i;
        NormalizeGpuMacsToBridge(hosts.Get(gpuindex),SubNetDevices[gpuindex]);
    }
}

bool ReadConf(std::string network_conf)
{
  using std::string;

  // ---------- 依赖：nlohmann/json ----------
  // 如果你是 <nlohmann/json.hpp>，把下一行改成 nlohmann/json.hpp
  // 并在文件顶部 #include。这里只写函数体，不重复 include。
  using nlohmann::json;

  // ---------- 打开并解析 JSON ----------
  std::ifstream in(network_conf);
  if (!in) {
    std::cerr << "[Config] cannot open: " << network_conf << "\n";
    return false;
  }
  json j;
  try {
    in >> j;
  } catch (const std::exception& e) {
    std::cerr << "[Config] parse error: " << e.what() << "\n";
    return false;
  }

  // ---------- 小工具 ----------
  auto JHas = [&](const json& x, const char* k){ return x.contains(k) && !x.at(k).is_null(); };
  auto JGetStr = [&](const json& x, const char* k, std::string& out){
    if (JHas(x,k)) out = x.at(k).get<std::string>();
  };
  auto to_upper = [](std::string s){ for(char& c:s) c = (char)std::toupper((unsigned char)c); return s; };

  auto ParseGpuType = [&](const std::string& sIn)->GPUType {
    std::string s = to_upper(sIn);
    if (s=="A100") return GPUType::A100;
    if (s=="H100") return GPUType::H100;
    // 需要其它类型在此扩展
    return GPUType::A100; // 默认
  };
  auto ParseTopoModel = [&](const json& top)->int {
    if (JHas(top,"model")) {
      if (top["model"].is_number_integer()) {
        int v = top["model"].get<int>();
        if (v>=0 && v<=4) return v;
      } else if (top["model"].is_string()) {
        std::string s = top["model"].get<std::string>(); 
        for (auto& c:s) c=(char)std::tolower((unsigned char)c);
        if (s=="fattree")  return 0;
        if (s=="clos")     return 1;
        if (s=="fullmesh") return 2;
        if (s=="torus")    return 3;
        if (s=="test")     return 4;
      }
    }
    return 0; // 默认 FatTree
  };

  // ---------- 备份旧值（失败时不改动） ----------
  auto gpu_type_bak             = gpu_type;
  auto gpus_per_server_bak      = gpus_per_server;
  auto nvswitch_per_server_bak  = nvswitch_per_server;
  auto ecmpmodel_bak            = ecmpmodel;
  auto max_jump_bak             = max_jump;
  auto floadtime_bak            = floadtime;
  auto m_mtu_bak                = m_mtu;
  auto normallink_rate_bak      = normallink_rate;
  auto nvlink_rate_bak          = nvlink_rate;
  auto normallink_delay_bak     = normallink_delay;
  auto nvlink_delay_bak         = nvlink_delay;
  auto DropTailQueue_size_bak   = DropTailQueue_size;
  auto Topology_model_bak       = Topology_model;
  auto k_bak                    = k_all;
  auto hostperleaf_bak          = hostperleaf;


  // ---------- 1) 顶层通用 ----------
  if (j.contains("workloadpath")) network_workload = j["workloadpath"].get<std::string>();
  if (j.contains("thread")) m_thread = j["thread"].get<int>();
  if (j.contains("gpu_type")) gpu_type = ParseGpuType(j["gpu_type"].get<std::string>());
  if (j.contains("gpus_per_server"))     gpus_per_server     = j["gpus_per_server"].get<uint32_t>();
  if (j.contains("nvswitch_per_server")) nvswitch_per_server = j["nvswitch_per_server"].get<uint32_t>();

  // ---------- 2) network ----------
  if (j.contains("network")) {
    const auto& nw = j["network"];
    if (JHas(nw,"ecmpmodel"))            ecmpmodel        = nw["ecmpmodel"].get<int>();
    if (JHas(nw,"max_jump"))             max_jump         = nw["max_jump"].get<int>();
    if (JHas(nw,"mtu"))                  m_mtu            = nw["mtu"].get<uint32_t>();
    JGetStr(nw, "normallink_rate",       normallink_rate);
    JGetStr(nw, "nvlink_rate",           nvlink_rate);
    JGetStr(nw, "normallink_delay",      normallink_delay);
    JGetStr(nw, "nvlink_delay",          nvlink_delay);
    JGetStr(nw, "drop_tail_queue_size",  DropTailQueue_size);
    JGetStr(nw, "drop_tail_queue_num",  DropTailQueue_num);
  }

  // ---------- 3) workload ----------
  if (j.contains("workload")) {
    const auto& wl = j["workload"];
    if (JHas(wl,"floadtime_s")) floadtime = wl["floadtime_s"].get<uint64_t>();
  }

  // ---------- 4) topology + 派生量 ----------
  uint64_t switch_delay_override = 0; // 0 表示不覆盖
  if (j.contains("topology")) {
    const auto& top = j["topology"];
    Topology_model = ParseTopoModel(top);
    if (JHas(top,"k"))   k_all   = top["k"].get<int>();
    if (JHas(top,"subnet_model"))   subnet_model   = top["subnet_model"].get<int>();
    if (JHas(top,"subnet_k"))   subnet_k   = top["subnet_k"].get<int>();
    if (JHas(top,"hostperleaf")) hostperleaf = top["hostperleaf"].get<int>();
    if (JHas(top,"switch_delay_ns_override"))
      switch_delay_override = top["switch_delay_ns_override"].is_null() ? 0ULL
                                 : top["switch_delay_ns_override"].get<uint64_t>();
  }

  // 计算派生量（与SetConfig一致）
    if (subnet_model==0){//nosubnet
        gpus_per_server=1;
        nvswitch_per_server=1;
    }
    if (subnet_model==1){//nvswitch
    }
    if (subnet_model==2){//fullmesh
        gpus_per_server=subnet_k;
        nvswitch_per_server=1;
    }
    if (subnet_model==3){//torus
        gpus_per_server=subnet_k*subnet_k;
        nvswitch_per_server=1;
    }
  switch (Topology_model) {
    case 0: { // FatTree
      k_fattree=k_all;
      numHosts        = k_fattree * k_fattree * k_fattree / 4;
      numCores        = (k_fattree/2) * (k_fattree/2);
      numAggregations = k_fattree * k_fattree / 2;
      numEdges        = k_fattree * k_fattree / 2;

      numGPUs      = numHosts * (int)gpus_per_server;
      nvswitch_num = numHosts * (int)nvswitch_per_server;
      switch_num   = numCores + numAggregations + numEdges;
      node_num     = numGPUs + switch_num + nvswitch_num;

      switch_delay = switch_delay_override ? switch_delay_override : 100; // ns
    } break;

    case 1: { // Clos
      k_clos=k_all;
      numSpine = k_clos / 2;  // （与你原注释一致）
      numLeaf  = k_clos;
      numHosts     = numLeaf * hostperleaf;

      numGPUs      = numHosts * (int)gpus_per_server;
      nvswitch_num = numHosts * (int)nvswitch_per_server;
      switch_num   = numSpine + numLeaf;
      node_num     = numGPUs + switch_num + nvswitch_num;

      switch_delay = switch_delay_override ? switch_delay_override : 5000; // ns
    } break;

    case 2: { // FullMesh
      k_mesh=k_all;
      num_meshswitch = k_mesh;
      numHosts     = k_mesh;

      numGPUs      = numHosts * (int)gpus_per_server;
      nvswitch_num = numHosts * (int)nvswitch_per_server;
      switch_num   = num_meshswitch;
      node_num     = numGPUs + switch_num + nvswitch_num;

      switch_delay = switch_delay_override ? switch_delay_override : 100; // ns
    } break;

    case 3: { // Torus
      k_torus=k_all;
      torus_x = k_torus, torus_y = k_torus;
      num_torusswitch = torus_x * torus_y;
      numHosts     = num_torusswitch;

      numGPUs      = numHosts * (int)gpus_per_server;
      nvswitch_num = numHosts * (int)nvswitch_per_server;
      switch_num   = num_torusswitch;
      node_num     = numGPUs + switch_num + nvswitch_num;

      switch_delay = switch_delay_override ? switch_delay_override : 100; // ns
    } break;

    case 4: { // Test
      num_testswitch = 1;
      numHosts     = k_test;

      numGPUs      = numHosts * (int)gpus_per_server;
      nvswitch_num = numHosts * (int)nvswitch_per_server;
      switch_num   = num_testswitch;
      node_num     = numGPUs + switch_num + nvswitch_num;

      switch_delay = switch_delay_override ? switch_delay_override : 100; // ns
    } break;

    default: {
      std::cerr << "[Config] unknown Topology_model: " << Topology_model << "\n";
      // 回滚备份
      gpu_type = gpu_type_bak; gpus_per_server = gpus_per_server_bak; nvswitch_per_server = nvswitch_per_server_bak;
      ecmpmodel = ecmpmodel_bak; max_jump = max_jump_bak; floadtime = floadtime_bak; m_mtu = m_mtu_bak;
      normallink_rate = normallink_rate_bak; nvlink_rate = nvlink_rate_bak;
      normallink_delay = normallink_delay_bak; nvlink_delay = nvlink_delay_bak;
      DropTailQueue_size = DropTailQueue_size_bak;
      Topology_model = Topology_model_bak;
      k_fattree = k_bak; k_clos = k_bak; hostperleaf = hostperleaf_bak;
      k_mesh = k_bak; k_torus = k_bak; k_test = k_bak;
      return false;
    }
  }

  return true;
}

void SetConfig(){
    gpu_type = GPUType::A100;
    gpus_per_server=1;
    nvswitch_per_server=1;
    ecmpmodel=1;//ECMP模式；0随机转发，1按包hash
    max_jump=5;//最大5跳
    floadtime=200;//泛红时间200秒
    m_mtu=4096;
    normallink_rate="1600Gbps";
    nvlink_rate="2400Gbps";
    normallink_delay="5ns";
    nvlink_delay="15ns";
    DropTailQueue_size="1000000";
    DropTailQueue_num="16";

    Topology_model=0;//0--FatTree;1--Clos;2--FullMesh;3--Torus;4--Test
    subnet_model=0;//0--nosubnet;1--nvswitch;2--FullMesh;3--Torus;
    subnet_k=4;
    //-----------------subnet_set----------------
    //nosubnet
    if (subnet_model==0){
        gpus_per_server=1;
        nvswitch_per_server=1;
    }
    //nvswitch
    if (subnet_model==1){
        //do nothing
    }
    //FullMesh
    if (subnet_model==2){
        gpus_per_server=subnet_k;
        nvswitch_per_server=1;
    }
    //Torus
    if (subnet_model==3){
        gpus_per_server=subnet_k*subnet_k;
        nvswitch_per_server=1;
    }

    //-----------------mainnet_set----------------
    //fattree
    if (Topology_model==0){
        k_fattree=4;//FatTree规模
        numHosts = k_fattree*k_fattree*k_fattree/4;//终端数量
        numCores = (k_fattree/2)*(k_fattree/2);//核心层交换机数量
        numAggregations = k_fattree*k_fattree/2;//汇聚层交换机数量
        numEdges = k_fattree*k_fattree/2;//边缘层交换机数量

        numGPUs=numHosts*gpus_per_server;
        nvswitch_num=numHosts*nvswitch_per_server;

        switch_num=numCores+numAggregations+numEdges;
        node_num=numGPUs+switch_num+nvswitch_num;
        
        switch_delay=100;//单位纳秒
    }
    //clos
    if (Topology_model==1){
        k_clos=8;
        hostperleaf=2;
        numSpine=k_clos/2;
        // numSpine=k_clos;
        numLeaf=k_clos;
        numHosts=numLeaf*hostperleaf;

        numGPUs=numHosts*gpus_per_server;
        nvswitch_num=numHosts*nvswitch_per_server;
        switch_num=numSpine+numLeaf;
        node_num=numGPUs+switch_num+nvswitch_num;

        switch_delay=5000;//单位纳秒
    }
    //fullmesh
    if (Topology_model==2){
        k_mesh=16;
        num_meshswitch=k_mesh;
        numHosts=k_mesh;
        numGPUs=numHosts*gpus_per_server;
        nvswitch_num=numHosts*nvswitch_per_server;
        switch_num=num_meshswitch;
        node_num=numGPUs+switch_num+nvswitch_num;

        switch_delay=100;//单位纳秒
    }
    //torus
    if (Topology_model==3){
        k_torus=4;
        torus_x=k_torus;
        torus_y=k_torus;
        
        num_torusswitch=torus_x*torus_y;
        numHosts=num_torusswitch;

        numGPUs=numHosts*gpus_per_server;
        nvswitch_num=numHosts*nvswitch_per_server;
        switch_num=num_torusswitch;
        node_num=numGPUs+switch_num+nvswitch_num;

        switch_delay=100;//单位纳秒
    }
    
    //test(仅调试用)
    if (Topology_model==4){
        k_test=16;
        num_testswitch=1;
        numHosts=k_test;

        numGPUs=numHosts*gpus_per_server;
        nvswitch_num=numHosts*nvswitch_per_server;
        switch_num=num_testswitch;
        node_num=numGPUs+switch_num+nvswitch_num;

        switch_delay=100;//单位纳秒
    }


}

void SetupNetwork(){
    NS_LOG_INFO("common now");
    
    // LogComponentEnable("MultithreadedSimulatorImpl", LOG_LEVEL_INFO);
    // LogComponentEnable("MtpInterface",LOG_LEVEL_INFO);
    // LogComponentEnable("LogicalProcess", LOG_LEVEL_INFO);
    // LogComponentEnable("MinHopBridgeNetDevice", LOG_LEVEL_INFO);
    // LogComponentEnable("SendFlowClient", LOG_LEVEL_INFO);
    // LogComponentEnable("HopCountClient", LOG_LEVEL_INFO);
    // LogComponentEnable("ThreePhaseProtocol", LOG_LEVEL_INFO);
    //创建整个网络的节点

    //FatTree
    if (Topology_model==0){
        NodeContainer coreSwitches;
        coreSwitches.Create(numCores);
        NodeContainer aggregationSwitches;
        aggregationSwitches.Create(numAggregations);
        NodeContainer edgeSwitches;
        edgeSwitches.Create(numEdges);
        NodeContainer hosts;
        hosts.Create(numGPUs);
        NodeContainer nvswitch;
        nvswitch.Create(nvswitch_num);

        #ifdef NS3_MPI
        uint32_t numRanks = MpiInterface::GetSize();
        for (uint32_t p = 0; p < k_fattree; ++p) { // 遍历所有 Pods (0 到 k-1)
            // 计算当前 Pod 应该分配给哪个 Rank (SystemId)
            uint32_t targetRank = p % numRanks;
            
            // 1.1 汇聚层交换机 (Aggregation Switches)
            for (uint32_t i = 0; i < k_fattree / 2; ++i) { 
                uint32_t aggIndex = p * (k_fattree / 2) + i;
                aggregationSwitches.Get(aggIndex)->SetSystemId(targetRank);
            }
            
            // 1.2 边缘层交换机 (Edge Switches)
            for (uint32_t i = 0; i < k_fattree / 2; ++i) { 
                uint32_t edgeIndex = p * (k_fattree / 2) + i;
                edgeSwitches.Get(edgeIndex)->SetSystemId(targetRank);
            }
            
            // 1.3 主机 (Hosts)
            for (uint32_t i = 0; i < k_fattree * k_fattree / 4; ++i) { 
                for(uint32_t j = 0; j < gpus_per_server ; ++j)
                {
                    uint32_t hostIndex = (p * (k_fattree * k_fattree / 4) + i) * gpus_per_server + j;
                    hosts.Get(hostIndex)->SetSystemId(targetRank);                
                }
                for(uint32_t j = 0; j < nvswitch_per_server ; ++j)
                {
                    uint32_t nvswitchIndex = (p * (k_fattree * k_fattree / 4) + i) * nvswitch_per_server +  j;
                    nvswitch.Get(nvswitchIndex)->SetSystemId(targetRank);
                }
            }
        }
        // 2. 核心层交换机分区 (Core Switches) - 轮询分配 (Round-Robin)
        for (uint32_t i = 0; i < numCores; ++i) {
            uint32_t targetRank = i % numRanks; // 核心交换机按索引轮询分配
            coreSwitches.Get(i)->SetSystemId(targetRank);
        }
        #endif


        //为所有交换机创建 MinHopBridgeNetDevice
        for (uint32_t i = 0; i < coreSwitches.GetN(); ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(max_jump);
            dev->set_ecmp_model(ecmpmodel);
            dev->Set_switchDelay(NanoSeconds(switch_delay));
            dev->SetMtu(m_mtu);
            coreSwitches.Get(i)->AddDevice(dev);
            coreDevices.push_back(dev);
        }
        for (uint32_t i = 0; i < aggregationSwitches.GetN(); ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(max_jump);
            dev->set_ecmp_model(ecmpmodel);
            dev->Set_switchDelay(NanoSeconds(switch_delay));
            dev->SetMtu(m_mtu);
            aggregationSwitches.Get(i)->AddDevice(dev);
            aggregationDevices.push_back(dev);
        }
        for (uint32_t i = 0; i < edgeSwitches.GetN(); ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(max_jump);
            dev->set_ecmp_model(ecmpmodel);
            dev->Set_switchDelay(NanoSeconds(switch_delay));
            dev->SetMtu(m_mtu);
            edgeSwitches.Get(i)->AddDevice(dev);
            edgeDevices.push_back(dev);
        }
        //为所有nvswitch安装特化的minhopbridge
        for (uint32_t i = 0; i < nvswitch.GetN(); ++i) {
            Ptr<MinHopBridgeNetDevice> nvs = CreateObject<MinHopBridgeNetDevice>();
            nvs->set_ecmp_model(ecmpmodel);
            nvs->Set_switchDelay(Seconds(0));
            nvs->SetMtu(m_mtu);
            nvswitch.Get(i)->AddDevice(nvs);
            nvswitchDevices.push_back(nvs);
        }

        // --- 3. 配置 P2P 链路 ---
        PointToPointHelper p2pHelper;
        p2pHelper.SetDeviceAttribute("DataRate", StringValue(normallink_rate));//链路带宽
        p2pHelper.SetChannelAttribute("Delay", StringValue(normallink_delay));
        // p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        p2pHelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue(DropTailQueue_size),
                       "NumQueues", StringValue(DropTailQueue_num));
        p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); // 支持 30KB 帧
        PointToPointHelper nvlinkhelper;
        nvlinkhelper.SetDeviceAttribute("DataRate", StringValue(nvlink_rate));
        nvlinkhelper.SetChannelAttribute("Delay", StringValue(nvlink_delay));//存疑
        // nvlinkhelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        nvlinkhelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue(DropTailQueue_size),
                       "NumQueues", StringValue(DropTailQueue_num));
        nvlinkhelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); 

        // --- 4. 连接：核心层 ↔ 汇聚层 ---
        for (uint32_t i = 0; i < k_fattree / 2; ++i) { // 核心层第 i 行
            for (uint32_t j = 0; j < k_fattree / 2; ++j) { // 核心层第 j 列
                uint32_t coreIndex = i * (k_fattree / 2) + j;
                for (uint32_t p = 0; p < k_fattree; ++p) { // 每个 Pod
                    uint32_t aggIndex = p * (k_fattree / 2) + i; // Pod p 的第 i 个 Aggregation Switch
                    NodeContainer link(coreSwitches.Get(coreIndex), aggregationSwitches.Get(aggIndex));
                    NetDeviceContainer devs = p2pHelper.Install(link);
                    coreDevices[coreIndex]->AddBridgePort(devs.Get(0));
                    aggregationDevices[aggIndex]->AddBridgePort(devs.Get(1));
                }
            }
        }
        // --- 5. 连接：汇聚层 ↔ 边缘层 ---
        for (uint32_t p = 0; p < k_fattree; ++p) {
            for (uint32_t i = 0; i < k_fattree / 2; ++i) {
                for (uint32_t j = 0; j < k_fattree / 2; ++j) {
                    uint32_t aggIndex = p * (k_fattree / 2) + i;
                    uint32_t edgeIndex = p * (k_fattree / 2) + j;
                    NodeContainer link(aggregationSwitches.Get(aggIndex), edgeSwitches.Get(edgeIndex));
                    NetDeviceContainer devs = p2pHelper.Install(link);
                    aggregationDevices[aggIndex]->AddBridgePort(devs.Get(0));
                    edgeDevices[edgeIndex]->AddBridgePort(devs.Get(1));
                }
            }
        }
        // --- 6. 连接：边缘层 ↔ 主机 ---
        std::vector<Ptr<NetDevice>> hostDevices; // 保存主机设备用于后续应用
        std::vector<ns3::Address> gpuaddress;
        
        //没有子网或者用nvswitch
        if (subnet_model==0 or subnet_model==1){
            for (uint32_t p = 0; p < k_fattree; ++p) {
                for (uint32_t i = 0; i < k_fattree / 2; ++i) {
                    for (uint32_t j = 0; j < k_fattree / 2; ++j) {
                        uint32_t edgeIndex = p * (k_fattree / 2) + i;
                        uint32_t hostIndex = p * (k_fattree * k_fattree / 4) + i * (k_fattree / 2) + j;
                        
                        for (uint32_t g=0;g<gpus_per_server;g++){//首先连接外部
                            uint32_t gpuindex=hostIndex*gpus_per_server+g;
                            //创建节点交换器
                            Ptr<MinHopBridgeNetDevice> br = CreateGpuSubnetBridge(hosts.Get(gpuindex),1);
                            SubNetDevices.push_back(br);

                            NodeContainer link(edgeSwitches.Get(edgeIndex), hosts.Get(gpuindex));
                            NetDeviceContainer devs = p2pHelper.Install(link);
                            edgeDevices[edgeIndex]->AddBridgePort(devs.Get(0));
                            hostDevices.push_back(devs.Get(1)); // 主机侧设备
                            gpuaddress.push_back(devs.Get(1)->GetAddress());
                        }
                        //然后创建子网拓扑
                        BuildServerSubnet_NvSwitch(hostIndex,hosts,nvswitch,nvlinkhelper);
                    }
                }
            }
            
        }
        //子网用fullmesh
        if (subnet_model==2){
           for (uint32_t p = 0; p < k_fattree; ++p) {
                for (uint32_t i = 0; i < k_fattree / 2; ++i) {
                    for (uint32_t j = 0; j < k_fattree / 2; ++j) {
                        uint32_t edgeIndex = p * (k_fattree / 2) + i;
                        uint32_t hostIndex = p * (k_fattree * k_fattree / 4) + i * (k_fattree / 2) + j;
                        //计算偏移量，否则因为重新编号的原因会把第一个子网反复构建，后面啥都没有
                        uint32_t subnet_offset=hostIndex*gpus_per_server;
                        for (uint32_t g=0;g<gpus_per_server;g++){//首先连接外部
                            uint32_t gpuindex=hostIndex*gpus_per_server+g;
                            //创建节点交换器
                            Ptr<MinHopBridgeNetDevice> br = CreateGpuSubnetBridge(hosts.Get(gpuindex),1);
                            SubNetDevices.push_back(br);

                            NodeContainer link(edgeSwitches.Get(edgeIndex), hosts.Get(gpuindex));
                            NetDeviceContainer devs = p2pHelper.Install(link);
                            edgeDevices[edgeIndex]->AddBridgePort(devs.Get(0));
                            hostDevices.push_back(devs.Get(1)); // 主机侧设备
                            gpuaddress.push_back(devs.Get(1)->GetAddress());
                        }
                        //然后创建子网拓扑
                        BuildServerSubnet_FullMesh(hostIndex,hosts,nvlinkhelper);
                    }
                }
           }           
        }
        //子网用torus
        if (subnet_model==3){
           for (uint32_t p = 0; p < k_fattree; ++p) {
                for (uint32_t i = 0; i < k_fattree / 2; ++i) {
                    for (uint32_t j = 0; j < k_fattree / 2; ++j) {
                        uint32_t edgeIndex = p * (k_fattree / 2) + i;
                        uint32_t hostIndex = p * (k_fattree * k_fattree / 4) + i * (k_fattree / 2) + j;
                        for (uint32_t g=0;g<gpus_per_server;g++){//首先连接外部
                            uint32_t gpuindex=hostIndex*gpus_per_server+g;
                            //创建节点交换器
                            Ptr<MinHopBridgeNetDevice> br = CreateGpuSubnetBridge(hosts.Get(gpuindex),1);
                            SubNetDevices.push_back(br);

                            NodeContainer link(edgeSwitches.Get(edgeIndex), hosts.Get(gpuindex));
                            NetDeviceContainer devs = p2pHelper.Install(link);
                            edgeDevices[edgeIndex]->AddBridgePort(devs.Get(0));
                            hostDevices.push_back(devs.Get(1)); // 主机侧设备
                            gpuaddress.push_back(devs.Get(1)->GetAddress());
                        }
                        //然后创建子网拓扑
                        BuildServerSubnet_Torus(hostIndex,hosts,nvlinkhelper);
                    }
                }
           }        
        }
        
        // 获取所有主机的 MAC 地址 ---
        std::vector<Mac48Address> hostMacs;
        for (uint32_t i = 0; i < hostDevices.size(); ++i) {
            Mac48Address mac = Mac48Address::ConvertFrom(hostDevices[i]->GetAddress());
            hostMacs.push_back(mac);
        }
        // 配置 PacketSocket 应用层 ---
        PacketSocketHelper packetSocketHelper;
        packetSocketHelper.Install(hosts); // 为所有主机安装 PacketSocket

        // 为每个主机安装发往其他主机的 HopCountClient
        uint16_t customEtherType = 0x88B5;
        for (uint32_t i = 0; i < numGPUs; ++i){
            Ptr<Node> clientNode = hosts.Get(i);
            #ifdef NS3_MPI
            if(clientNode->GetSystemId() != MpiInterface::GetSystemId())
            {
                continue;
            }
            #endif
            for (uint32_t k=0;k<=1;k++){
                PacketSocketAddress clientSocketAddr;
                clientSocketAddr.SetSingleDevice(k);
                clientSocketAddr.SetPhysicalAddress(hostMacs[i]);
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
                clientApps.Stop(Seconds(floadtime-1));
            }           
        }
        for (uint32_t i = 0; i < numGPUs; ++i) {
            Ptr<Node> clientNode = hosts.Get(i);
            uint16_t proto=0x9999;
            Ptr<sendflowclient> App = CreateObject<sendflowclient>();
            App->SetStartTime(Seconds(floadtime-1));
            App->SetStopTime(Seconds(2000000000));
            hosts.Get(i)->AddApplication(App);

            App->initialization(proto,1,0,SubNetDevices[i]);
            
            PacketSocketAddress clientSocketAddr;
            //这个没问题，如果有子网也会统一到本地交换机的地址，也是0号地址
            clientSocketAddr.SetPhysicalAddress(hosts.Get(i)->GetDevice(0)->GetAddress());
            clientSocketAddr.SetProtocol(proto);

            nodeaddresstable[i]=clientSocketAddr;
            nodeapptable[i]=App;
            nodetable[i] = clientNode;
        }
    }
    
    //Clos
    if (Topology_model==1){
        NodeContainer SpineSwitchs;
        SpineSwitchs.Create(numSpine);
        NodeContainer LeafSwitchs;
        LeafSwitchs.Create(numLeaf);
        NodeContainer hosts;
        hosts.Create(numGPUs);
        NodeContainer nvswitch;
        nvswitch.Create(nvswitch_num);
        
        for (uint32_t i = 0; i < numSpine; ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(max_jump);
            dev->set_ecmp_model(ecmpmodel);
            dev->Set_switchDelay(NanoSeconds(switch_delay));
            dev->SetMtu(m_mtu);
            SpineSwitchs.Get(i)->AddDevice(dev);
            SpineDevices.push_back(dev);
        }
        for (uint32_t i = 0; i < numLeaf; ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(max_jump);
            dev->set_ecmp_model(ecmpmodel);
            dev->Set_switchDelay(NanoSeconds(switch_delay));
            dev->SetMtu(m_mtu);
            LeafSwitchs.Get(i)->AddDevice(dev);
            LeafDevices.push_back(dev);
        }
        for (uint32_t i = 0; i < nvswitch.GetN(); ++i) {
            Ptr<MinHopBridgeNetDevice> nvs = CreateObject<MinHopBridgeNetDevice>();
            nvs->set_ecmp_model(ecmpmodel);
            nvs->Set_switchDelay(Seconds(0));
            nvs->SetMtu(m_mtu);
            nvswitch.Get(i)->AddDevice(nvs);
            nvswitchDevices.push_back(nvs);
        }

        PointToPointHelper p2pHelper;
        p2pHelper.SetDeviceAttribute("DataRate", StringValue(normallink_rate));//链路带宽
        p2pHelper.SetChannelAttribute("Delay", StringValue(normallink_delay));
        // p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        p2pHelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue(DropTailQueue_size),
                       "NumQueues", StringValue(DropTailQueue_num));
        p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); // 支持 30KB 帧
        PointToPointHelper nvlinkhelper;
        nvlinkhelper.SetDeviceAttribute("DataRate", StringValue(nvlink_rate));
        nvlinkhelper.SetChannelAttribute("Delay", StringValue(nvlink_delay));//存疑
        // nvlinkhelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        nvlinkhelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue(DropTailQueue_size),
                       "NumQueues", StringValue(DropTailQueue_num));
        nvlinkhelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); 

        for (uint32_t i=0;i<numSpine;i++){
            uint32_t spineindex=i;
            for (uint32_t j=0;j<numLeaf;j++){
                uint32_t leafindex=j;
                NodeContainer link(SpineSwitchs.Get(spineindex), LeafSwitchs.Get(leafindex));
                NetDeviceContainer devs = p2pHelper.Install(link);
                SpineDevices[spineindex]->AddBridgePort(devs.Get(0));
                LeafDevices[leafindex]->AddBridgePort(devs.Get(1));
            }
        }
        
        std::vector<Ptr<NetDevice>> hostDevices; // 保存主机设备用于后续应用
        std::vector<ns3::Address> gpuaddress;
        //没有子网或者用nvswitch
        if (subnet_model==0 or subnet_model==1){
            for (uint32_t i=0;i<numLeaf;i++){
                uint32_t leafindex=i;
                for (uint32_t j=0;j<hostperleaf;j++){
                    uint32_t hostIndex=leafindex*hostperleaf+j;
                    for (uint32_t g=0;g<gpus_per_server;g++){//首先连接外部
                        uint32_t gpuindex=hostIndex*gpus_per_server+g;
                        //创建节点交换器
                        Ptr<MinHopBridgeNetDevice> br = CreateGpuSubnetBridge(hosts.Get(gpuindex),1);
                        SubNetDevices.push_back(br);
                        NodeContainer link(LeafSwitchs.Get(leafindex), hosts.Get(gpuindex));
                        NetDeviceContainer devs = p2pHelper.Install(link);
                        LeafDevices[leafindex]->AddBridgePort(devs.Get(0));
                        hostDevices.push_back(devs.Get(1)); // 主机侧设备
                        gpuaddress.push_back(devs.Get(1)->GetAddress());
                    }
                    //然后创建子网拓扑
                    BuildServerSubnet_NvSwitch(hostIndex,hosts,nvswitch,nvlinkhelper);
                }
            }
        }
        //子网用fullmesh
        if (subnet_model==2){
            for (uint32_t i=0;i<numLeaf;i++){
                uint32_t leafindex=i;
                for (uint32_t j=0;j<hostperleaf;j++){
                    uint32_t hostIndex=leafindex*hostperleaf+j;
                    for (uint32_t g=0;g<gpus_per_server;g++){//首先连接外部
                        uint32_t gpuindex=hostIndex*gpus_per_server+g;
                        //创建节点交换器
                        Ptr<MinHopBridgeNetDevice> br = CreateGpuSubnetBridge(hosts.Get(gpuindex),1);
                        SubNetDevices.push_back(br);

                        NodeContainer link(LeafSwitchs.Get(leafindex), hosts.Get(gpuindex));
                        NetDeviceContainer devs = p2pHelper.Install(link);
                        LeafDevices[leafindex]->AddBridgePort(devs.Get(0));
                        hostDevices.push_back(devs.Get(1)); // 主机侧设备
                        gpuaddress.push_back(devs.Get(1)->GetAddress());
                    }
                    //然后创建子网拓扑
                    BuildServerSubnet_FullMesh(hostIndex,hosts,nvlinkhelper);
                }
            }
        }
        //子网用torus
        if (subnet_model==3){
            for (uint32_t i=0;i<numLeaf;i++){
                uint32_t leafindex=i;
                for (uint32_t j=0;j<hostperleaf;j++){
                    uint32_t hostIndex=leafindex*hostperleaf+j;
                    for (uint32_t g=0;g<gpus_per_server;g++){//首先连接外部
                        uint32_t gpuindex=hostIndex*gpus_per_server+g;
                        //创建节点交换器
                        Ptr<MinHopBridgeNetDevice> br = CreateGpuSubnetBridge(hosts.Get(gpuindex),subnet_k);
                        SubNetDevices.push_back(br);

                        NodeContainer link(LeafSwitchs.Get(leafindex), hosts.Get(gpuindex));
                        NetDeviceContainer devs = p2pHelper.Install(link);
                        LeafDevices[leafindex]->AddBridgePort(devs.Get(0));
                        hostDevices.push_back(devs.Get(1)); // 主机侧设备
                        gpuaddress.push_back(devs.Get(1)->GetAddress());
                    }
                    //然后创建子网拓扑
                    BuildServerSubnet_Torus(hostIndex,hosts,nvlinkhelper);
                }
            }    
        }
        
         // 获取所有主机的 MAC 地址 ---
        std::vector<Mac48Address> hostMacs;
        for (uint32_t i = 0; i < hostDevices.size(); ++i) {
            Mac48Address mac = Mac48Address::ConvertFrom(hostDevices[i]->GetAddress());
            hostMacs.push_back(mac);
        }
        // --- 8. 配置 PacketSocket 应用层 ---
        PacketSocketHelper packetSocketHelper;
        packetSocketHelper.Install(hosts); // 为所有主机安装 PacketSocket


        // 为每个主机安装发往其他主机的 HopCountClient
        uint16_t customEtherType = 0x88B5;
        for (uint32_t i = 0; i < numGPUs; ++i){
            Ptr<Node> clientNode = hosts.Get(i);
            #ifdef NS3_MPI
            if(clientNode->GetSystemId() != MpiInterface::GetSystemId())
            {
                continue;
            }
            #endif
            for (uint32_t k=0;k<=1;k++){
                PacketSocketAddress clientSocketAddr;
                clientSocketAddr.SetSingleDevice(k);
                clientSocketAddr.SetPhysicalAddress(hostMacs[i]);
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
                clientApps.Stop(Seconds(floadtime-1));
            }           
        }

        for (uint32_t i = 0; i < numGPUs; ++i) {
            Ptr<Node> clientNode = hosts.Get(i);
            uint16_t proto=0x9999;
            Ptr<sendflowclient> App = CreateObject<sendflowclient>();
            App->SetStartTime(Seconds(floadtime-1));
            App->SetStopTime(Seconds(2000000000));
            hosts.Get(i)->AddApplication(App);

            App->initialization(proto,1,0,SubNetDevices[i]);
            
            PacketSocketAddress clientSocketAddr;
            //这个没问题，如果有子网也会统一到本地交换机的地址，也是0号地址
            clientSocketAddr.SetPhysicalAddress(hosts.Get(i)->GetDevice(0)->GetAddress());
            clientSocketAddr.SetProtocol(proto);

            nodeaddresstable[i]=clientSocketAddr;
            nodeapptable[i]=App;
            nodetable[i] = clientNode;
        }
    }
    
    //fullmesh和torus改了没测试过
    //fullmesh
    if (Topology_model==2){
        NodeContainer hosts;
        hosts.Create(numGPUs);

        #ifdef NS3_MPI
        uint32_t numRanks = MpiInterface::GetSize();
        uint32_t gpuPerRank = numGPUs/numRanks;
        for(uint32_t i = 0; i < numGPUs ; i++){
            uint32_t targetRank = i / gpuPerRank;
            hosts.Get(i) -> SetSystemId(targetRank);
        }        
        #endif

        PointToPointHelper p2pHelper;
        p2pHelper.SetDeviceAttribute("DataRate", StringValue(normallink_rate));//链路带宽
        p2pHelper.SetChannelAttribute("Delay", StringValue(normallink_delay));
        // p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        p2pHelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue(DropTailQueue_size),
                       "NumQueues", StringValue(DropTailQueue_num));
        p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); // 支持 30KB 帧

        //创建内部bridge
        for (uint32_t i = 0; i < num_meshswitch; ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(1);
            dev->set_ecmp_model(ecmpmodel);
            dev->Set_switchDelay(NanoSeconds(switch_delay));
            dev->SetMtu(m_mtu);
            hosts.Get(i)->AddDevice(dev);
            FullmeshDevices.push_back(dev);
        }
        //构建拓扑
        for (uint32_t i=0;i<num_meshswitch-1;i++){
            for (uint32_t j=i+1;j<num_meshswitch;j++){
                if (i==j) continue;
                NodeContainer link(hosts.Get(i), hosts.Get(j));
                NetDeviceContainer devs = p2pHelper.Install(link);
                FullmeshDevices[i]->AddBridgePort(devs.Get(0));
                FullmeshDevices[j]->AddBridgePort(devs.Get(1));
            }
        }
        // 统一地址
        for (uint32_t g=0;g<numGPUs;g++){
            uint32_t gpuindex=g;
            NormalizeGpuMacsToBridge(hosts.Get(gpuindex),FullmeshDevices[gpuindex]);
        }
        // 获取所有主机的 MAC 地址 ---
        std::vector<Mac48Address> hostMacs;
        for (uint32_t i=0;i<numGPUs;i++){
            Mac48Address mac = Mac48Address::ConvertFrom(FullmeshDevices[i]->GetAddress());
            hostMacs.push_back(mac);
        }

        // 配置 PacketSocket 应用层 ---
        PacketSocketHelper packetSocketHelper;
        packetSocketHelper.Install(hosts); // 为所有主机安装 PacketSocket


        // 为每个主机安装发往其他主机的 HopCountClient
        uint16_t customEtherType = 0x88B5;
        for (uint32_t i = 0; i < numGPUs; ++i){
            #ifdef NS3_MPI
            if(hosts.Get(i)->GetSystemId() != MpiInterface::GetSystemId())
            {
                continue;
            }
            #endif
            PacketSocketAddress clientSocketAddr;
            clientSocketAddr.SetSingleDevice(0);
            clientSocketAddr.SetPhysicalAddress(hostMacs[i]);
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
            clientApps.Stop(Seconds(floadtime-1));
        }
        for (uint32_t i = 0; i < numGPUs; ++i) {
            Ptr<Node> clientNode = hosts.Get(i);
            uint16_t proto=0x9999;
            Ptr<sendflowclient> App = CreateObject<sendflowclient>();
            App->SetStartTime(Seconds(floadtime-1));
            App->SetStopTime(Seconds(2000000000));
            hosts.Get(i)->AddApplication(App);

            App->initialization(proto,0,SubNetDevices[i]);

            PacketSocketAddress clientSocketAddr;
            clientSocketAddr.SetPhysicalAddress(hosts.Get(i)->GetDevice(0)->GetAddress());
            clientSocketAddr.SetProtocol(proto);

            nodeaddresstable[i]=clientSocketAddr;
            nodeapptable[i]=App;
            nodetable[i] = clientNode;
        }
    }
    
    //torus
    if (Topology_model==3){
        NodeContainer TorusSwitch;
        TorusSwitch.Create(num_torusswitch);
  
        PointToPointHelper p2pHelper;
        p2pHelper.SetDeviceAttribute("DataRate", StringValue(normallink_rate));//链路带宽
        p2pHelper.SetChannelAttribute("Delay", StringValue(normallink_delay));
        // p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        p2pHelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue(DropTailQueue_size),
                       "NumQueues", StringValue(DropTailQueue_num));
        p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); // 支持 30KB 帧

        //创建内部bridge
        for (uint32_t i = 0; i < num_torusswitch; ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(k_torus);
            dev->set_ecmp_model(ecmpmodel);
            dev->Set_switchDelay(NanoSeconds(switch_delay));
            dev->SetMtu(m_mtu);
            TorusSwitch.Get(i)->AddDevice(dev);
            TorusDevices.push_back(dev);
        }
        //构建拓扑
        for (uint32_t x=0;x<torus_x;x++){
            for (uint32_t y=0;y<torus_y;y++){
                uint32_t idnow=x*torus_y+y;
                uint32_t id_right=x*torus_y+((y+1)%torus_y);
                uint32_t id_down=((x+1)%torus_x)*torus_y+y;

                NodeContainer link_x(TorusSwitch.Get(idnow), TorusSwitch.Get(id_right));
                NetDeviceContainer devs_x = p2pHelper.Install(link_x);
                TorusDevices[idnow]->AddBridgePort(devs_x.Get(0));
                TorusDevices[id_right]->AddBridgePort(devs_x.Get(1));

                NodeContainer link_y(TorusSwitch.Get(idnow), TorusSwitch.Get(id_down));
                NetDeviceContainer devs_y = p2pHelper.Install(link_y);
                TorusDevices[idnow]->AddBridgePort(devs_y.Get(0));
                TorusDevices[id_down]->AddBridgePort(devs_y.Get(1));
            }
        }
        // 统一地址
        for (uint32_t g=0;g<numGPUs;g++){
            uint32_t gpuindex=g;
            NormalizeGpuMacsToBridge(TorusSwitch.Get(gpuindex),TorusDevices[gpuindex]);
        }
        // 获取所有主机的 MAC 地址 ---
        std::vector<Mac48Address> hostMacs;
        for (uint32_t i=0;i<numGPUs;i++){
            Mac48Address mac = Mac48Address::ConvertFrom(TorusDevices[i]->GetAddress());
            hostMacs.push_back(mac);
        }
        
        // 配置 PacketSocket 应用层 ---
        PacketSocketHelper packetSocketHelper;
        packetSocketHelper.Install(TorusSwitch); // 为所有主机安装 PacketSocket


        // 为每个主机安装发往其他主机的 HopCountClient
        uint16_t customEtherType = 0x88B5;
        for (uint32_t i = 0; i < numGPUs; ++i){
            PacketSocketAddress clientSocketAddr;
            clientSocketAddr.SetSingleDevice(0);
            clientSocketAddr.SetPhysicalAddress(hostMacs[i]);
            clientSocketAddr.SetProtocol(customEtherType);
            Ptr<HopCountClient> client = CreateObject<HopCountClient>();
            client->SetRemote(clientSocketAddr);
            client->SetAttribute("PacketSize", UintegerValue(100));
            client->SetAttribute("Interval", TimeValue(Seconds(1.5)));
            client->SetAttribute("MaxPackets", UintegerValue(1));
            TorusSwitch.Get(i)->AddApplication(client);

            double startTime = 1.0 + (i * 0.2);
            ApplicationContainer clientApps(client);
            clientApps.Start(Seconds(startTime));
            clientApps.Stop(Seconds(floadtime-1));
        }
        for (uint32_t i = 0; i < numGPUs; ++i) {
            Ptr<Node> clientNode = TorusSwitch.Get(i);
            uint16_t proto=0x9999;
            Ptr<sendflowclient> App = CreateObject<sendflowclient>();
            App->SetStartTime(Seconds(floadtime-1));
            App->SetStopTime(Seconds(2000000000));
            TorusSwitch.Get(i)->AddApplication(App);

            App->initialization(proto,0,SubNetDevices[i]);
            
            PacketSocketAddress clientSocketAddr;
            clientSocketAddr.SetPhysicalAddress(TorusSwitch.Get(i)->GetDevice(0)->GetAddress());
            clientSocketAddr.SetProtocol(proto);

            // nodeaddresstable[i]=clientSocketAddr;
            // nodeapptable[i]=App;
            // nodetable[i] = clientNode;

            if (k_torus==4){
                nodeaddresstable[i]=clientSocketAddr;
                nodeapptable[i]=App;
            }
            else {
                if (i>=0 and i<=3){
                    nodeaddresstable[i]=clientSocketAddr;
                    nodeapptable[i]=App;
                }
                if (i>=8 and i<=11){
                    nodeaddresstable[i-4]=clientSocketAddr;
                    nodeapptable[i-4]=App;
                }
                if (i>=16 and i<=19){
                    nodeaddresstable[i-8]=clientSocketAddr;
                    nodeapptable[i-8]=App;
                }
                if (i>=24 and i<=27){
                    nodeaddresstable[i-12]=clientSocketAddr;
                    nodeapptable[i-12]=App;
                }
            }
        }
    }

    //test(仅调试用)
    if (Topology_model==4){
        
    }
}


void printlearnstate(){
    NS_LOG_INFO("--------- 打印所有 主机 的地址 ---------");
    if (Topology_model==0 or Topology_model==1){
        for (auto dev : SubNetDevices) dev->Print_LocalMacAddress();
    }

    NS_LOG_INFO("--------- 打印所有 MinHopBridgeNetDevice 的学习状态 ---------");
    if (Topology_model==0){//FatTree
        for (auto dev : coreDevices) dev->PrintLearnedState();
        for (auto dev : aggregationDevices) dev->PrintLearnedState();
        for (auto dev : edgeDevices) dev->PrintLearnedState();
        // for (auto dev : SubNetDevices) dev->PrintLearnedState();
        for (auto dev : nvswitchDevices) dev->PrintLearnedState();
    }
    if (Topology_model==1){//Clos
        for (auto dev : SpineDevices) dev->PrintLearnedState();
        for (auto dev : LeafDevices) dev->PrintLearnedState();
    }
    if (Topology_model==2){//FullMesh
        for (auto dev : FullmeshDevices) dev->PrintLearnedState();
    }
    if (Topology_model==3){//Torus
        for (auto dev : TorusDevices) dev->PrintLearnedState();
    }
    if (Topology_model==4){//Test
        
    }
    
    
    NS_LOG_INFO("--------------------------------------------------");
}





#endif