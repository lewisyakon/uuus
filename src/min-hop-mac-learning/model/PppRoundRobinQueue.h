#ifndef PPP_ROUND_ROBIN_QUEUE_H
#define PPP_ROUND_ROBIN_QUEUE_H

#include "ns3/queue.h"
#include "ns3/packet.h"
#include "ns3/ppp-header.h" 
#include "ns3/mac48-address.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <functional>
#include "ns3/random-variable-stream.h"
#include "ns3/hop-count-header.h"
#include "3PHeader.h"

namespace ns3 {

/**
 * \brief Ppp Round Robin Queue (ECMP-like)
 */
template <typename Item>
class PppRoundRobinQueue : public Queue<Item>
{
public:
  // Public interface and TypeId
  static TypeId GetTypeId ();
  PppRoundRobinQueue ();
  ~PppRoundRobinQueue () override;

  // Queue virtual methods override
  bool Enqueue (Ptr<Item> item) override;
  Ptr<Item> Dequeue () override;
  Ptr<Item> Remove () override;
  Ptr<const Item> Peek () const override;

  uint32_t GetNPackets (void) const;
  uint32_t GetNBytes (void) const;

private:
  // Private members (using explicit template definition in .h)
  using Queue<Item>::GetContainer;
  using Queue<Item>::DoEnqueue;
  using Queue<Item>::DoDequeue;
  using Queue<Item>::DoRemove;
  using Queue<Item>::DoPeek;

  NS_LOG_TEMPLATE_DECLARE; //!< Redefinition of the log component

  // Custom attributes and variables
  uint32_t m_numQueues;
  uint32_t m_rrIndex; // Round-Robin pointer
  uint32_t m_maxPacketsPerQueue;
  
  std::vector<std::deque<Ptr<Item>>> m_subQueues;
  std::deque<Ptr<Item>> m_priority_que;
  uint32_t m_currentTotalPackets;

  Ptr<UniformRandomVariable> m_queueSelector;

  // Custom private methods
  uint32_t CalculateQueueIndex (Ptr<const Item> p);

  std::map<ns3::Mac48Address,uint32_t> address_to_index;
  uint32_t work_model;
  uint32_t work2index;
};

// --- Template Implementation ---
template <typename Item>
uint32_t
PppRoundRobinQueue<Item>::GetNPackets (void) const
{
    // 直接返回我们精确维护的计数器
    return m_currentTotalPackets;
}

template <typename Item>
uint32_t
PppRoundRobinQueue<Item>::GetNBytes (void) const
{
    // 假设只关注包的数量，字节数返回 0 是安全的。
    return 0; 
}


template <typename Item>
TypeId
PppRoundRobinQueue<Item>::GetTypeId (void)
{
  static TypeId tid =
    TypeId (GetTemplateClassName<PppRoundRobinQueue<Item>>())
    .SetParent<Queue<Item>> ()
    .SetGroupName ("Network")
    .template AddConstructor<PppRoundRobinQueue<Item>> () // Note the 'template' keyword
    .AddAttribute ("NumQueues", "Number of internal queues",
                   UintegerValue (4),
                   MakeUintegerAccessor (&PppRoundRobinQueue<Item>::m_numQueues),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxPacketsPerQueue", "Max packets per sub-queue",
                   UintegerValue (100),
                   MakeUintegerAccessor (&PppRoundRobinQueue<Item>::m_maxPacketsPerQueue),
                   MakeUintegerChecker<uint32_t> ());
  return tid;
}

template <typename Item>
PppRoundRobinQueue<Item>::PppRoundRobinQueue ()
  : Queue<Item>(),
    m_rrIndex (0),
    m_currentTotalPackets (0),
    m_numQueues (4),
    m_maxPacketsPerQueue (100),
    work_model(0),
    work2index(0),
    NS_LOG_TEMPLATE_DEFINE("PppRoundRobinQueue") // Logging definition
{
  NS_LOG_FUNCTION(this);
  m_queueSelector = CreateObject<UniformRandomVariable> ();
}

template <typename Item>
PppRoundRobinQueue<Item>::~PppRoundRobinQueue ()
{
  NS_LOG_FUNCTION(this);
  m_subQueues.clear ();
}

// Custom private methods (Implementation relies on Item being Packet)

template <typename Item>
uint32_t
PppRoundRobinQueue<Item>::CalculateQueueIndex (Ptr<const Item> p)
{
  Ptr<const Packet> packet = DynamicCast<const Packet, const Item>(p);
  if (!packet)
    {
      NS_LOG_WARN("Item is not a Packet. Using default queue 0.");
      return 0; 
    }

  PppHeader pppHeader;
  Ptr<Packet> pp = packet->Copy();
  pp->RemoveHeader(pppHeader);

  Address dst = pppHeader.GetDstMac ();
  Mac48Address mac = Mac48Address::ConvertFrom (dst);
  uint8_t buffer[6];
  mac.CopyTo (buffer);
  
  work_model=2;

  //随机
  if (work_model==0){
    auto nowsys = std::chrono::system_clock::now();
    auto duration = nowsys.time_since_epoch();
    uint64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    uint32_t seed = static_cast<uint32_t>(now);
    return seed % m_numQueues;

    // uint32_t index = m_queueSelector->GetInteger(0, m_numQueues - 1);
    // return index;
  }
  //按地址哈希
  if (work_model==1){
    // 将 6 个 MAC 地址字节组合成一个 64 位无符号整数 (uint64_t)。
    uint64_t mac_as_u64 = 0;
    for (int i = 0; i < 6; ++i){
      mac_as_u64 = (mac_as_u64 << 8) | buffer[i];
    }
    std::hash<uint64_t> hasher;
    uint32_t final_hash = static_cast<uint32_t>(hasher(mac_as_u64));
    return final_hash% m_numQueues;
  }
  //按地址顺序存储
  if (work_model==2){
    if (address_to_index.find(mac)!=address_to_index.end()) return address_to_index[mac];
    else{
      work2index=(work2index+1)%m_numQueues;
      address_to_index[mac]=work2index;
      return work2index;
    }
  }


  
}


// Queue virtual methods implementation

template <typename Item>
bool
PppRoundRobinQueue<Item>::Enqueue(Ptr<Item> item)
{
  NS_LOG_FUNCTION(this << item);

  if (m_subQueues.size () != m_numQueues)
    {
      m_subQueues.resize (m_numQueues);
    }
  
  Ptr<const Packet> packet = DynamicCast<const Packet, const Item>(item);
  Ptr<Packet> pp = packet->Copy();
  PppHeader pppHeader;
  HopCountHeader hopHeader;
  ThreePHeader TPheader;
  pp->RemoveHeader(pppHeader);
  pp->RemoveHeader(hopHeader);  // 移除头部以读取状态
  pp->RemoveHeader(TPheader);  // 移除头部以读取状态
  if (TPheader.GetState()==ThreePHeader::REQUEST or TPheader.GetState()==ThreePHeader::ACT){
    m_currentTotalPackets++;
    m_priority_que.push_back(item);
    return true;
  }
  

  uint32_t idx = CalculateQueueIndex (item);

  if (m_subQueues[idx].size () >= m_maxPacketsPerQueue)
    {
      NS_LOG_LOGIC ("Sub-queue " << idx << " full. Dropping item.");
      this->DropBeforeEnqueue (item); 
      return false;
    }

  m_subQueues[idx].push_back (item);
  m_currentTotalPackets++;
  
  return true;
}

template <typename Item>
Ptr<Item>
PppRoundRobinQueue<Item>::Dequeue()
{
  NS_LOG_FUNCTION(this);
  if (m_currentTotalPackets == 0) return nullptr;
  if (!m_priority_que.empty()){
    Ptr<Item> p=m_priority_que.front();
    m_priority_que.pop_front();
    m_currentTotalPackets--;
    return p;
  }
  for (uint32_t i = 0; i < m_numQueues; ++i)
    {
      uint32_t currentIdx = (m_rrIndex + i) % m_numQueues;
      if (!m_subQueues[currentIdx].empty ())
        {
          Ptr<Item> p = m_subQueues[currentIdx].front ();
          m_subQueues[currentIdx].pop_front ();
          m_currentTotalPackets--;
          
          m_rrIndex = (currentIdx + 1) % m_numQueues;
          // this->Dequeued(p);
          // this->Queue<Item>::Dequeued(p);
          
          NS_LOG_LOGIC("Popped " << p << " from sub-queue " << currentIdx);
          return p;
        }
    }
  return nullptr;
}

template <typename Item>
Ptr<Item>
PppRoundRobinQueue<Item>::Remove()
{
  // Remove is often synonymous with Dequeue in simple queues, 
  // but ensures the item is tracked as removed/dropped if successful.
  Ptr<Item> p = Dequeue();
  if (p)
    {
      // this->Dropped (p); 
      // this->Queue<Item>::Dropped(p);
    }
  return p;
}

template <typename Item>
Ptr<const Item>
PppRoundRobinQueue<Item>::Peek() const
{
  NS_LOG_FUNCTION(this);
  if (m_currentTotalPackets == 0 || m_subQueues.empty()) return nullptr;

  for (uint32_t i = 0; i < m_numQueues; ++i)
    {
      uint32_t currentIdx = (m_rrIndex + i) % m_numQueues;
      if (!m_subQueues[currentIdx].empty ())
        {
          // Return without popping/moving m_rrIndex
          return m_subQueues[currentIdx].front ();
        }
    }
  return nullptr;
}

// Declaration to prevent implicit instantiation (actual instantiation is in .cc)
extern template class PppRoundRobinQueue<Packet>;
extern template class PppRoundRobinQueue<QueueDiscItem>;

} // namespace ns3

#endif /* PPP_ROUND_ROBIN_QUEUE_H */