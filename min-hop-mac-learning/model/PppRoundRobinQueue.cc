#include "PppRoundRobinQueue.h"
#include "ns3/log.h"


namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PppRoundRobinQueue");

// 强制实例化并注册最常用的模板特化：PppRoundRobinQueue<Packet>
// 这是 ns-3 网络设备通常需要的类型。
NS_OBJECT_TEMPLATE_CLASS_DEFINE(PppRoundRobinQueue, Packet);

// 如果您的系统使用了 Traffic Control (QueueDiscs)，可能还需要这个：
// NS_OBJECT_TEMPLATE_CLASS_DEFINE(PppRoundRobinQueue, QueueDiscItem);

} // namespace ns3