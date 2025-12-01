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
#include "ns3/mpi-interface.h"

#include <mutex>
#include <map>

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("GENERIC_SIMULATION");

GPUType gpu_type;
int ecmpmodel,max_jump,floadtime,m_mtu;
uint32_t node_num, switch_num, link_num, trace_num, nvswitch_num, gpus_per_server,
            nvswitch_per_server,numGPUs;
uint32_t Topology_model,numHosts;
string normallink_rate,nvlink_rate,normallink_delay,nvlink_delay,DropTailQueue_size;
uint32_t k_fattree,numCores,numAggregations,numEdges;
uint32_t k_clos,numSpine,numLeaf,hostperleaf;
uint32_t k_torus,torus_x,torus_y,num_torusswitch;
uint32_t k_mesh,num_meshswitch;
uint32_t k_test,num_testswitch;


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

bool ReadConf(string network_topo,string network_conf){
    return 1;
}
bool ReadConf(){
    return 1;
}
void SetConfig(){
    gpu_type = GPUType::A100;
    gpus_per_server=1;
    nvswitch_per_server=1;
    ecmpmodel=1;//ECMP模式；0随机转发，1按包hash
    max_jump=5;//最大5跳
    floadtime=200;//泛红时间200秒
    m_mtu=4096;
    normallink_rate="224Gbps";
    nvlink_rate="2400Gbps";
    normallink_delay="5ns";
    nvlink_delay="15ns";
    DropTailQueue_size="1000000p";

    Topology_model=2;//0--FatTree;1--Clos;2--FullMesh;3--Torus;4--Test
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
    }
    //clos
    if (Topology_model==1){
        k_clos=8;
        hostperleaf=16;
        numSpine=k_clos/2;
        // numSpine=k_clos;
        numLeaf=k_clos;
        numHosts=numLeaf*hostperleaf;

        numGPUs=numHosts*gpus_per_server;
        nvswitch_num=numHosts*nvswitch_per_server;
        switch_num=numSpine+numLeaf;
        node_num=numGPUs+switch_num+nvswitch_num;
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

        //为所有交换机创建 MinHopBridgeNetDevice
        for (uint32_t i = 0; i < coreSwitches.GetN(); ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(max_jump);
            dev->set_ecmp_model(ecmpmodel);
            coreSwitches.Get(i)->AddDevice(dev);
            coreDevices.push_back(dev);
        }
        for (uint32_t i = 0; i < aggregationSwitches.GetN(); ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(max_jump);
            dev->set_ecmp_model(ecmpmodel);
            aggregationSwitches.Get(i)->AddDevice(dev);
            aggregationDevices.push_back(dev);
        }
        for (uint32_t i = 0; i < edgeSwitches.GetN(); ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(max_jump);
            dev->set_ecmp_model(ecmpmodel);
            edgeSwitches.Get(i)->AddDevice(dev);
            edgeDevices.push_back(dev);
        }
        //为所有nvswitch安装特化的minhopbridge
        for (uint32_t i = 0; i < nvswitch.GetN(); ++i) {
            Ptr<MinHopBridgeNetDevice> nvs = CreateObject<MinHopBridgeNetDevice>();
            nvs->set_ecmp_model(ecmpmodel);
            nvs->Set_switchDelay(Seconds(0));
            nvswitch.Get(i)->AddDevice(nvs);
            nvswitchDevices.push_back(nvs);
        }

        // --- 3. 配置 P2P 链路 ---
        PointToPointHelper p2pHelper;
        p2pHelper.SetDeviceAttribute("DataRate", StringValue(normallink_rate));//链路带宽
        p2pHelper.SetChannelAttribute("Delay", StringValue(normallink_delay));
        // p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        p2pHelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue("10000000"),
                       "NumQueues", StringValue("16"));
        p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); // 支持 30KB 帧
        PointToPointHelper nvlinkhelper;
        nvlinkhelper.SetDeviceAttribute("DataRate", StringValue(nvlink_rate));
        nvlinkhelper.SetChannelAttribute("Delay", StringValue(nvlink_delay));//存疑
        // nvlinkhelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        p2pHelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue("10000000"),
                       "NumQueues", StringValue("16"));
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
        for (uint32_t p = 0; p < k_fattree; ++p) {
            for (uint32_t i = 0; i < k_fattree / 2; ++i) {
                for (uint32_t j = 0; j < k_fattree / 2; ++j) {
                    uint32_t edgeIndex = p * (k_fattree / 2) + i;
                    uint32_t hostIndex = p * (k_fattree * k_fattree / 4) + i * (k_fattree / 2) + j;
                    
                    for (uint32_t g=0;g<gpus_per_server;g++){//首先连接外部
                        uint32_t gpuindex=hostIndex*gpus_per_server+g;
                        NodeContainer link(edgeSwitches.Get(edgeIndex), hosts.Get(gpuindex));
                        NetDeviceContainer devs = p2pHelper.Install(link);
                        edgeDevices[edgeIndex]->AddBridgePort(devs.Get(0));
                        hostDevices.push_back(devs.Get(1)); // 主机侧设备
                        gpuaddress.push_back(devs.Get(1)->GetAddress());
                    }
                    for (uint32_t k=0;k<nvswitch_per_server;k++){
                        uint32_t nvswitchindex=hostIndex*nvswitch_per_server+k;
                        for (uint32_t g=0;g<gpus_per_server;g++){
                            uint32_t gpuindex=hostIndex*gpus_per_server+g;
                            NodeContainer link(nvswitch.Get(nvswitchindex), hosts.Get(gpuindex));
                            NetDeviceContainer devs = nvlinkhelper.Install(link);
                            nvswitchDevices[nvswitchindex]->AddBridgePort(devs.Get(0));

                            devs.Get(1)->SetAttribute("Address",Mac48AddressValue(ns3::Mac48Address::ConvertFrom(gpuaddress[gpuindex])));
                            //这里应该在nvswitch里直接插入一条学习记录
                            std::vector<uint32_t> ports;
                            ports.push_back(nvswitch.Get(nvswitchindex)->GetNDevices()-2);//这个可能有点问题
                            nvswitchDevices[nvswitchindex]->AddEntry(ns3::Mac48Address::ConvertFrom(gpuaddress[gpuindex]),ports);
                        }
                    }
                }
            }
        }
        // --- 7. 获取所有主机的 MAC 地址 ---
        std::vector<Mac48Address> hostMacs;
        // NS_LOG_INFO("--- 主机 MAC 地址列表 ---");
        for (uint32_t i = 0; i < hostDevices.size(); ++i) {
            Mac48Address mac = Mac48Address::ConvertFrom(hostDevices[i]->GetAddress());
            hostMacs.push_back(mac);
            NS_LOG_INFO("主机 " << i << " 的 MAC 地址: " << mac);
        }
        // --- 8. 配置 PacketSocket 应用层 ---
        PacketSocketHelper packetSocketHelper;
        packetSocketHelper.Install(hosts); // 为所有主机安装 PacketSocket


        // 为每个主机安装发往其他主机的 HopCountClient
        uint16_t customEtherType = 0x88B5;
        // NS_LOG_INFO("为所有主机安装 HopCountClient 实现全网通信...");
        for (uint32_t i = 0; i < numGPUs; ++i){
            PacketSocketAddress clientSocketAddr;
            clientSocketAddr.SetSingleDevice(hostDevices[i]->GetIfIndex());
            clientSocketAddr.SetPhysicalAddress(hostMacs[numGPUs-i]);
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
            uint16_t proto=0x9999;
            Ptr<sendflowclient> App = CreateObject<sendflowclient>();
            App->Setup_out_device(uint32_t(0));

            for (uint32_t j=1;j<hosts.Get(i)->GetNDevices();j++) App->addinerdevice(j);
            
            hosts.Get(i)->AddApplication(App);
            App->initialization();
            App->SetStartTime(Seconds(floadtime-1));
            App->SetStopTime(Seconds(2000000000));

            PacketSocketAddress clientSocketAddr;
            // clientSocketAddr.SetSingleDevice(uint32_t(0));
            clientSocketAddr.SetPhysicalAddress(hosts.Get(i)->GetDevice(0)->GetAddress());
            clientSocketAddr.SetProtocol(proto);

            nodeaddresstable[i]=clientSocketAddr;
            nodeapptable[i]=App;
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
            SpineSwitchs.Get(i)->AddDevice(dev);
            SpineDevices.push_back(dev);
        }
        for (uint32_t i = 0; i < numLeaf; ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(max_jump);
            dev->set_ecmp_model(ecmpmodel);
            LeafSwitchs.Get(i)->AddDevice(dev);
            LeafDevices.push_back(dev);
        }
        for (uint32_t i = 0; i < nvswitch.GetN(); ++i) {
            Ptr<MinHopBridgeNetDevice> nvs = CreateObject<MinHopBridgeNetDevice>();
            nvs->set_ecmp_model(ecmpmodel);
            nvs->Set_switchDelay(Seconds(0));
            nvswitch.Get(i)->AddDevice(nvs);
            nvswitchDevices.push_back(nvs);
        }

        PointToPointHelper p2pHelper;
        p2pHelper.SetDeviceAttribute("DataRate", StringValue(normallink_rate));//链路带宽
        p2pHelper.SetChannelAttribute("Delay", StringValue(normallink_delay));
        p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        // p2pHelper.SetQueue("ns3::PppRoundRobinQueue", 
        //                "MaxPacketsPerQueue", StringValue("10000000"),
        //                "NumQueues", StringValue("16"));
        p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); // 支持 30KB 帧
        PointToPointHelper nvlinkhelper;
        nvlinkhelper.SetDeviceAttribute("DataRate", StringValue(nvlink_rate));
        nvlinkhelper.SetChannelAttribute("Delay", StringValue(nvlink_delay));//存疑
        nvlinkhelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        // nvlinkhelper.SetQueue("ns3::PppRoundRobinQueue", 
        //                "MaxPacketsPerQueue", StringValue("10000000"),
        //                "NumQueues", StringValue("16"));
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
        for (uint32_t i=0;i<numLeaf;i++){
            uint32_t leafindex=i;
            for (uint32_t j=0;j<hostperleaf;j++){
                uint32_t hostindex=leafindex*hostperleaf+j;
                for (uint32_t g=0;g<gpus_per_server;g++){//首先连接外部
                    uint32_t gpuindex=hostindex*gpus_per_server+g;
                    NodeContainer link(LeafSwitchs.Get(leafindex), hosts.Get(gpuindex));
                    NetDeviceContainer devs = p2pHelper.Install(link);
                    LeafDevices[leafindex]->AddBridgePort(devs.Get(0));
                    hostDevices.push_back(devs.Get(1)); // 主机侧设备
                    gpuaddress.push_back(devs.Get(1)->GetAddress());
                }
                for (uint32_t k=0;k<nvswitch_per_server;k++){
                    uint32_t nvswitchindex=hostindex*nvswitch_per_server+k;
                    for (uint32_t g=0;g<gpus_per_server;g++){
                        uint32_t gpuindex=hostindex*gpus_per_server+g;
                        NodeContainer link(nvswitch.Get(nvswitchindex), hosts.Get(gpuindex));
                        NetDeviceContainer devs = nvlinkhelper.Install(link);
                        nvswitchDevices[nvswitchindex]->AddBridgePort(devs.Get(0));

                        devs.Get(1)->SetAttribute("Address",Mac48AddressValue(ns3::Mac48Address::ConvertFrom(gpuaddress[gpuindex])));
                        //这里应该在nvswitch里直接插入一条学习记录
                        std::vector<uint32_t> ports;
                        ports.push_back(nvswitch.Get(nvswitchindex)->GetNDevices()-2);//这个可能有点问题
                        nvswitchDevices[nvswitchindex]->AddEntry(ns3::Mac48Address::ConvertFrom(gpuaddress[gpuindex]),ports);
                    }
                }
            }
        }
         // --- 7. 获取所有主机的 MAC 地址 ---
        std::vector<Mac48Address> hostMacs;
        for (uint32_t i = 0; i < hostDevices.size(); ++i) {
            Mac48Address mac = Mac48Address::ConvertFrom(hostDevices[i]->GetAddress());
            hostMacs.push_back(mac);
            NS_LOG_INFO("主机 " << i << " 的 MAC 地址: " << mac);
        }
        // --- 8. 配置 PacketSocket 应用层 ---
        PacketSocketHelper packetSocketHelper;
        packetSocketHelper.Install(hosts); // 为所有主机安装 PacketSocket


        // 为每个主机安装发往其他主机的 HopCountClient
        uint16_t customEtherType = 0x88B5;
        // NS_LOG_INFO("为所有主机安装 HopCountClient 实现全网通信...");
        for (uint32_t i = 0; i < numGPUs; ++i){
            PacketSocketAddress clientSocketAddr;
            clientSocketAddr.SetSingleDevice(hostDevices[i]->GetIfIndex());
            clientSocketAddr.SetPhysicalAddress(hostMacs[numGPUs-i]);
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
            uint16_t proto=0x9999;
            Ptr<sendflowclient> App = CreateObject<sendflowclient>();
            App->Setup_out_device(uint32_t(0));

            for (uint32_t j=1;j<hosts.Get(i)->GetNDevices();j++) App->addinerdevice(j);
            
            hosts.Get(i)->AddApplication(App);
            App->initialization();
            App->SetStartTime(Seconds(floadtime-1));
            App->SetStopTime(Seconds(2000000000));

            PacketSocketAddress clientSocketAddr;
            // clientSocketAddr.SetSingleDevice(uint32_t(0));
            clientSocketAddr.SetPhysicalAddress(hosts.Get(i)->GetDevice(0)->GetAddress());
            clientSocketAddr.SetProtocol(proto);

            nodeaddresstable[i]=clientSocketAddr;
            nodeapptable[i]=App;
        }
    }
    
    //fullmesh
    if (Topology_model==2){
        NodeContainer hosts;
        hosts.Create(numGPUs);

        uint32_t numRanks = MpiInterface::GetSize();
        uint32_t gpuPerRank = numGPUs/numRanks;
        for(uint32_t i = 0; i < numGPUs ; i++){
            uint32_t targetRank = i / gpuPerRank;
            hosts.Get(i) -> SetSystemId(targetRank);
        }        

        for (uint32_t i = 0; i < num_meshswitch; ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            // dev->set_maxjump(max_jump);
            dev->set_maxjump(1);
            dev->set_ecmp_model(ecmpmodel);
            dev->Set_switchDelay(NanoSeconds(100));
            dev->SetMtu(m_mtu);
            hosts.Get(i)->AddDevice(dev);
            FullmeshDevices.push_back(dev);
        }

        PointToPointHelper p2pHelper;
        p2pHelper.SetDeviceAttribute("DataRate", StringValue(normallink_rate));//链路带宽
        p2pHelper.SetChannelAttribute("Delay", StringValue(normallink_delay));
        // p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        p2pHelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue("10000000"),
                       "NumQueues", StringValue("16"));
        p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); // 支持 30KB 帧

        for (uint32_t i=0;i<num_meshswitch-1;i++){
            for (uint32_t j=i+1;j<num_meshswitch;j++){
                if (i==j) continue;
                NodeContainer link(hosts.Get(i), hosts.Get(j));
                NetDeviceContainer devs = p2pHelper.Install(link);
                FullmeshDevices[i]->AddBridgePort(devs.Get(0));
                FullmeshDevices[j]->AddBridgePort(devs.Get(1));
            }
        }
        // --- 7. 获取所有主机的 MAC 地址 ---
        std::vector<Mac48Address> hostMacs;
        for (uint32_t i=0;i<numGPUs;i++){
            Mac48Address mac = Mac48Address::ConvertFrom(FullmeshDevices[i]->GetAddress());
            hostMacs.push_back(mac);
            NS_LOG_INFO("主机 " << i << " 的 MAC 地址: " << mac);
            for (uint32_t j=1;j<hosts.Get(i)->GetNDevices();j++){
                hosts.Get(i)->GetDevice(j)->SetAddress(mac);
            }
        }

        // --- 8. 配置 PacketSocket 应用层 ---
        PacketSocketHelper packetSocketHelper;
        packetSocketHelper.Install(hosts); // 为所有主机安装 PacketSocket


        // 为每个主机安装发往其他主机的 HopCountClient
        uint16_t customEtherType = 0x88B5;
        // NS_LOG_INFO("为所有主机安装 HopCountClient 实现全网通信...");
        for (uint32_t i = 0; i < numGPUs; ++i){
            if(hosts.Get(i)->GetSystemId() != MpiInterface::GetSystemId())
            {
                continue;
            }
            for (uint32_t j = 0; j < numGPUs-1; ++j){
                PacketSocketAddress clientSocketAddr;
                clientSocketAddr.SetSingleDevice(1+j);
                clientSocketAddr.SetPhysicalAddress(hostMacs[numGPUs-i]);
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
            uint16_t proto=0x9999;
            Ptr<sendflowclient> App = CreateObject<sendflowclient>();
            // App->Setup_out_device(uint32_t(1));
            App->set_localbridge(FullmeshDevices[i]);
            App->set_isfullortorus(1);
            for (uint32_t j=1;j<hosts.Get(i)->GetNDevices();j++) App->addinerdevice(j);
            
            hosts.Get(i)->AddApplication(App);
            App->initialization();
            App->SetStartTime(Seconds(floadtime-1));
            App->SetStopTime(Seconds(2000000000));

            PacketSocketAddress clientSocketAddr;
            // clientSocketAddr.SetSingleDevice(uint32_t(0));
            clientSocketAddr.SetPhysicalAddress(hosts.Get(i)->GetDevice(0)->GetAddress());
            clientSocketAddr.SetProtocol(proto);

            nodeaddresstable[i]=clientSocketAddr;
            nodeapptable[i]=App;
            nodetable[i] = hosts.Get(i);
        }
    }
    
    //torus
    if (Topology_model==3){
        NodeContainer TorusSwitch;
        TorusSwitch.Create(num_torusswitch);
        nvswitch_num=16;

        
        for (uint32_t i = 0; i < num_torusswitch; ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(k_torus);
            dev->set_ecmp_model(ecmpmodel);
            dev->Set_switchDelay(NanoSeconds(100));
            dev->SetMtu(m_mtu);
            TorusSwitch.Get(i)->AddDevice(dev);
            TorusDevices.push_back(dev);
        }

        PointToPointHelper p2pHelper;
        p2pHelper.SetDeviceAttribute("DataRate", StringValue(normallink_rate));//链路带宽
        p2pHelper.SetChannelAttribute("Delay", StringValue(normallink_delay));
        // p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        p2pHelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue("10000000"),
                       "NumQueues", StringValue("16"));
        p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); // 支持 30KB 帧


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
        // --- 7. 获取所有主机的 MAC 地址 ---
        std::vector<Mac48Address> hostMacs;
        for (uint32_t i=0;i<numGPUs;i++){
            Mac48Address mac = Mac48Address::ConvertFrom(TorusDevices[i]->GetAddress());
            hostMacs.push_back(mac);
            NS_LOG_INFO("主机 " << i << " 的 MAC 地址: " << mac);
            for (uint32_t j=1;j<TorusSwitch.Get(i)->GetNDevices();j++){
                TorusSwitch.Get(i)->GetDevice(j)->SetAddress(mac);
            }
        }
        
        // --- 8. 配置 PacketSocket 应用层 ---
        PacketSocketHelper packetSocketHelper;
        packetSocketHelper.Install(TorusSwitch); // 为所有主机安装 PacketSocket


        // 为每个主机安装发往其他主机的 HopCountClient
        uint16_t customEtherType = 0x88B5;
        // NS_LOG_INFO("为所有主机安装 HopCountClient 实现全网通信...");
        for (uint32_t i = 0; i < numGPUs; ++i){
            for (uint32_t j = 1; j < TorusSwitch.Get(i)->GetNDevices(); ++j){
                PacketSocketAddress clientSocketAddr;
                clientSocketAddr.SetSingleDevice(j);
                clientSocketAddr.SetPhysicalAddress(Mac48Address("00:00:00:00:00:03"));
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
        }
        for (uint32_t i = 0; i < numGPUs; ++i) {
            uint16_t proto=0x9999;
            Ptr<sendflowclient> App = CreateObject<sendflowclient>();
            // App->Setup_out_device(uint32_t(0));
            App->set_localbridge(TorusDevices[i]);
            App->set_isfullortorus(1);
            for (uint32_t j=1;j<TorusSwitch.Get(i)->GetNDevices();j++) App->addinerdevice(j);
            
            TorusSwitch.Get(i)->AddApplication(App);
            App->initialization();
            App->SetStartTime(Seconds(floadtime-1));
            App->SetStopTime(Seconds(2000000000));

            PacketSocketAddress clientSocketAddr;
            // clientSocketAddr.SetSingleDevice(uint32_t(0));
            clientSocketAddr.SetPhysicalAddress(TorusSwitch.Get(i)->GetDevice(0)->GetAddress());
            clientSocketAddr.SetProtocol(proto);

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
        NodeContainer TestSwitchs;
        TestSwitchs.Create(num_testswitch);
        NodeContainer hosts;
        hosts.Create(numGPUs);
        NodeContainer nvswitch;
        nvswitch.Create(nvswitch_num);

        for (uint32_t i = 0; i < num_testswitch; ++i) {
            Ptr<MinHopBridgeNetDevice> dev = CreateObject<MinHopBridgeNetDevice>();
            dev->set_maxjump(max_jump);
            dev->set_ecmp_model(ecmpmodel);
            dev->Set_switchDelay(NanoSeconds(5000));
            TestSwitchs.Get(i)->AddDevice(dev);
            TestSwitchDevices.push_back(dev);
        }

        for (uint32_t i = 0; i < nvswitch.GetN(); ++i) {
            Ptr<MinHopBridgeNetDevice> nvs = CreateObject<MinHopBridgeNetDevice>();
            nvs->set_ecmp_model(ecmpmodel);
            nvs->Set_switchDelay(Seconds(0));
            nvswitch.Get(i)->AddDevice(nvs);
            nvswitchDevices.push_back(nvs);
        }

        PointToPointHelper p2pHelper;
        p2pHelper.SetDeviceAttribute("DataRate", StringValue(normallink_rate));//链路带宽
        p2pHelper.SetChannelAttribute("Delay", StringValue(normallink_delay));
        // p2pHelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        p2pHelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue("10000000"),
                       "NumQueues", StringValue("16"));
        p2pHelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); // 支持 30KB 帧
        PointToPointHelper nvlinkhelper;
        nvlinkhelper.SetDeviceAttribute("DataRate", StringValue(nvlink_rate));
        nvlinkhelper.SetChannelAttribute("Delay", StringValue(nvlink_delay));//存疑
        // nvlinkhelper.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(DropTailQueue_size)); 
        p2pHelper.SetQueue("ns3::PppRoundRobinQueue", 
                       "MaxPacketsPerQueue", StringValue("10000000"),
                       "NumQueues", StringValue("16"));
        nvlinkhelper.SetDeviceAttribute("Mtu", UintegerValue(m_mtu)); 

        std::vector<Ptr<NetDevice>> hostDevices; // 保存主机设备用于后续应用
        std::vector<ns3::Address> gpuaddress;
        for (uint32_t i=0;i<numGPUs;i++){
            uint32_t hostindex=i;
            for (uint32_t g=0;g<gpus_per_server;g++){//首先连接外部
                uint32_t gpuindex=hostindex*gpus_per_server+g;
                NodeContainer link(TestSwitchs.Get(0), hosts.Get(gpuindex));
                NetDeviceContainer devs = p2pHelper.Install(link);
                TestSwitchDevices[0]->AddBridgePort(devs.Get(0));
                hostDevices.push_back(devs.Get(1)); // 主机侧设备
                gpuaddress.push_back(devs.Get(1)->GetAddress());
            }
            for (uint32_t k=0;k<nvswitch_per_server;k++){
                uint32_t nvswitchindex=hostindex*nvswitch_per_server+k;
                for (uint32_t g=0;g<gpus_per_server;g++){
                    uint32_t gpuindex=hostindex*gpus_per_server+g;
                    NodeContainer link(nvswitch.Get(nvswitchindex), hosts.Get(gpuindex));
                    NetDeviceContainer devs = nvlinkhelper.Install(link);
                    nvswitchDevices[nvswitchindex]->AddBridgePort(devs.Get(0));

                    devs.Get(1)->SetAttribute("Address",Mac48AddressValue(ns3::Mac48Address::ConvertFrom(gpuaddress[gpuindex])));
                    //这里应该在nvswitch里直接插入一条学习记录
                    std::vector<uint32_t> ports;
                    ports.push_back(nvswitch.Get(nvswitchindex)->GetNDevices()-2);//这个可能有点问题
                    nvswitchDevices[nvswitchindex]->AddEntry(ns3::Mac48Address::ConvertFrom(gpuaddress[gpuindex]),ports);
                }
            }
        }
         // --- 7. 获取所有主机的 MAC 地址 ---
        std::vector<Mac48Address> hostMacs;
        for (uint32_t i = 0; i < hostDevices.size(); ++i) {
            Mac48Address mac = Mac48Address::ConvertFrom(hostDevices[i]->GetAddress());
            hostMacs.push_back(mac);
            NS_LOG_INFO("主机 " << i << " 的 MAC 地址: " << mac);
        }
        // --- 8. 配置 PacketSocket 应用层 ---
        PacketSocketHelper packetSocketHelper;
        packetSocketHelper.Install(hosts); // 为所有主机安装 PacketSocket


        // 为每个主机安装发往其他主机的 HopCountClient
        uint16_t customEtherType = 0x88B5;
        // NS_LOG_INFO("为所有主机安装 HopCountClient 实现全网通信...");
        for (uint32_t i = 0; i < numGPUs; ++i){
            PacketSocketAddress clientSocketAddr;
            clientSocketAddr.SetSingleDevice(hostDevices[i]->GetIfIndex());
            clientSocketAddr.SetPhysicalAddress(hostMacs[numGPUs-i]);
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
            uint16_t proto=0x9999;
            Ptr<sendflowclient> App = CreateObject<sendflowclient>();
            App->Setup_out_device(uint32_t(0));

            for (uint32_t j=1;j<hosts.Get(i)->GetNDevices();j++) App->addinerdevice(j);
            
            hosts.Get(i)->AddApplication(App);
            App->initialization();
            App->SetStartTime(Seconds(floadtime-1));
            App->SetStopTime(Seconds(2000000000));

            PacketSocketAddress clientSocketAddr;
            // clientSocketAddr.SetSingleDevice(uint32_t(0));
            clientSocketAddr.SetPhysicalAddress(hosts.Get(i)->GetDevice(0)->GetAddress());
            clientSocketAddr.SetProtocol(proto);

            nodeaddresstable[i]=clientSocketAddr;
            nodeapptable[i]=App;
        }
    }
}
void printlearnstate(){
    NS_LOG_INFO("--- 打印所有 MinHopBridgeNetDevice 的学习状态 ---");
    if (Topology_model==0){//FatTree
        for (auto dev : coreDevices) dev->PrintLearnedState();
        for (auto dev : aggregationDevices) dev->PrintLearnedState();
        for (auto dev : edgeDevices) dev->PrintLearnedState();
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