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

#ifndef __ENTRY_H__
#define __ENTRY_H__

#undef PGO_TRAINING
#define PATH_TO_PGO_CONFIG "path_to_pgo_config"
#define _QPS_PER_CONNECTION_  1
#include "my_common.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/error-model.h"
#include "ns3/global-route-manager.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/packet.h"
#include "ns3/point-to-point-helper.h"

#include <fstream>
#include <iostream>
#include <time.h>
#include <unordered_map>
#include <mutex>
#include <vector>
#ifdef NS3_MTP
#include "ns3/mtp-interface.h"
#endif
#include <map>
#include"astra-sim/system/MockNcclQps.h"
#include "astra-sim/system/MockNcclLog.h"
using namespace ns3;
using namespace std;

std::mutex mtx;
struct task1 {
  int src;
  int dest;
  int type;
  uint64_t count;
  void *fun_arg;
  void (*msg_handler)(void *fun_arg);
  double schTime; 
};
map<std::pair<int, int>, int64_t> nodeHash;
extern uint32_t numHosts,gpus_per_server,Topology_model;


int check_out_or_iner(int src,int dst){//0表示在外部，1表示内部,3表示显示制定nvswitch,4表示无条件送原地址,5表示送本地交换机
  if (Topology_model==2 or Topology_model==3) return 5;
  if (src>numHosts or dst>numHosts) return 3;
  else if (src/gpus_per_server == dst/gpus_per_server) return 1;
       else return 0;
}

void SendFlow(int src, int dst, uint64_t maxPacketCount,
void (*msg_handler)(void *fun_arg), void *fun_arg, int tag, AstraSim::sim_request *request) {
  // NS_LOG_INFO("[SendFlow]: now_rank="<<MpiInterface::GetSystemId()<<" src_rank="<<(nodetable[src]->GetSystemId()>>16)<<" tag="<<tag<<" src="<<src<<" dst="<<dst<<" size="<<maxPacketCount);

  MockNcclLog*NcclLog = MockNcclLog::getInstance();
  uint64_t real_PacketCount = maxPacketCount;
  
  int flow_id = request->flowTag.current_flow_id;
  bool nvls_on = request->flowTag.nvls_on;

  if(real_PacketCount == 0) real_PacketCount = 1;
    // MockNcclLog* NcclLog = MockNcclLog::getInstance();
    // NcclLog->writeLog(NcclLogLevel::DEBUG," [Packet sending event]  %dSendFlow to  %d channelid:  %d flow_id  %d srcip  %d dstip  %d size:  %llu at the tick:  %d",src,dst,tag,flow_id,serverAddress[src],serverAddress[dst],maxPacketCount,AstraSim::Sys::boostedTick());
    // NcclLog->writeLog(NcclLogLevel::DEBUG," request->flowTag [Packet sending event]  %dSendFlow to  %d tag_id:  %d flow_id  %d srcip  %d dstip  %d size:  %llu at the tick:  %d",request->flowTag.sender_node,request->flowTag.receiver_node,request->flowTag.tag_id,request->flowTag.current_flow_id,serverAddress[src],serverAddress[dst],maxPacketCount,AstraSim::Sys::boostedTick());
  
  if(real_PacketCount == 1024)
  {
    Ptr<sendflowclient> clientHelper=nodeapptable[src];
    clientHelper->set_sendfunc(tag,src,dst,{msg_handler,fun_arg});

    clientHelper->TriggerSendTo_now(nodeaddresstable[dst], real_PacketCount,
      tag, src, dst, check_out_or_iner(src, dst));
  }
  else
  {
    Ptr<sendflowclient> clientHelper=nodeapptable[src];
    AstraSim::SendPacketEventHandlerData* ehd = (AstraSim::SendPacketEventHandlerData*) fun_arg;
    // AstraSim::RecvPacketEventHadndlerData* ehd = (AstraSim::RecvPacketEventHadndlerData*) fun_arg;
    ehd->flowTag=request->flowTag;
    clientHelper->set_sendfunc(request->flowTag.tag_id,src,dst,{msg_handler,fun_arg});
    clientHelper->write_flowtag(tag,src,dst,*request); 

    


    if(nvls_on) nvls_on=1;
    
    clientHelper = nodeapptable[src];
    clientHelper->TriggerSendTo_now(nodeaddresstable[dst], real_PacketCount,
      request->flowTag.tag_id, src, dst, check_out_or_iner(src,dst));

  }
  // ns3::fuc_and_par funcnow=clientHelper->get_recvfunc();
  // //不能在这里记录，这个时候这个点的recv函数还没被填充
  // AstraSim::RecvPacketEventHadndlerData* ehd2 = (AstraSim::RecvPacketEventHadndlerData*) funcnow.fun_arg;
  // ehd2->flowTag=request->flowTag;
  // clientHelper->set_recvfunc(flow_id,funcnow);

  // NcclLog->writeLog(NcclLogLevel::DEBUG,"waiting_to_notify_receiver  current_flow_id  %d src  %d dst  %d count  %d",request->flowTag.current_flow_id,src,dst,waiting_to_notify_receiver[std::make_pair(request->flowTag.tag_id,std::make_pair(src,dst))]);
}


// int main1(string network_topo,string network_conf) {
int main1() {
  clock_t begint, endt;
  begint = clock();

  // if (!ReadConf(network_topo,network_conf)) return -1;
  if (!ReadConf()) return -1;
  SetConfig();
  SetupNetwork();

std::cout << "Running Simulation.\n";
  fflush(stdout);
  NS_LOG_INFO("Run Simulation.");    

  endt = clock();
  return 0;
}
#endif