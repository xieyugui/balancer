/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef BALANCER_H_29177589_32F1_4D93_AE4F_1E140EDCC273
#define BALANCER_H_29177589_32F1_4D93_AE4F_1E140EDCC273

#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/experimental.h>
#include <string>

// Return the length of a string literal.
template <int N>
unsigned
lengthof(const char(&)[N])
{
  return N - 1;
}

struct BalancerTarget {
  uint id;
  std::string name;
  uint port;

  //add by daemon.xie
  uint weight;  //配置的权重
  int effective_weight;
  int current_weight; //当前权重，ats 会在运行过程中调整次权重


  uint max_fails; //最大失败次数

  time_t fail_timeout; //失败后，不再使用的时间
  uint down; //指定某个后端是否挂了
  uint backup;  //是否为备份线路

  uint fails; //已尝试失败次数
  uint timeout_fails;//当停用fail_timeout后，仍然是失败时+1,最大次数不能超过100
  time_t accessed; //检测失败时间，用于计算超时
  time_t checked;
};

//typedef struct BalancerTargetPeersS BalancerTargetPeersB;
//
//struct BalancerTargetPeersS {
//	uint number; //队列中服务器数量,为1表示后端服务器总共一台，用于优化，此时不需要再做选择
//	uint total_weight;//所有服务器总权重
////	unsigned single; //为1表示后端服务器总共一台，用于优化，此时不需要再做选择
////	unsigned weighted; //为1表示总的权重值等于服务器数量
////
////	std::string name;
////
////	BalancerTargetPeersT *next;//后备服务器挂载在这个字段下
////	BalancerTarget peer[1];
//
//};



struct BalancerInstance {
  virtual ~BalancerInstance() {}
  virtual void push_target(BalancerTarget &) = 0;
  virtual BalancerTarget &balance(TSHttpTxn, TSRemapRequestInfo *) = 0;
  virtual bool is_roundrobin_balancer() = 0;
  virtual TSReturnCode os_response_back_status(uint target_id, TSHttpStatus status) = 0;
};

//用于存储target 状态，以备源站返回code 的做健康负载处理，new  free
struct BalancerTargetStatus {

	BalancerInstance *binstance;
	uint target_id;
	uint target_down;
	uint is_down_check;
	TSCacheLookupResult object_status;
};

BalancerInstance *MakeHashBalancer(const char *);
BalancerInstance *MakeRoundRobinBalancer(const char *);

#endif /* BALANCER_H_29177589_32F1_4D93_AE4F_1E140EDCC273 */
