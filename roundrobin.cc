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

#include "balancer.h"
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#define MAX_FAIL_TIME  30
#define FAIL_STATUS 500
#define OS_SINGLE 1

namespace {
struct RoundRobinBalancer: public BalancerInstance {

	typedef std::map<uint, BalancerTarget> MapBalancerTarget;

	RoundRobinBalancer() :
			targets_s(), targets_b() {
		this->next = 0;
		this->is_balancer = true;
		this->peersS_number = 0;
		this->peersB_number = 0;
		this->path = NULL;
	}

	~RoundRobinBalancer() {
		if (this->path != NULL) {
			free((char *) this->path);
			this->path = NULL;
		}
		TSDebug("balancer","----------~RoundRobinBalancer---------------");
	}

	void push_target(BalancerTarget &target) {
		if (target.backup) {
			this->targets_b.push_back(target);
			this->peersB_number++;
		} else {
			this->targets_s.push_back(target);
			this->peersS_number++;
		}
	}

	BalancerTarget & //获取一个后端
	balance(TSHttpTxn, TSRemapRequestInfo *) {
		BalancerTarget *peer;
		time_t now;
		now = TShrtime() / TS_HRTIME_SECOND;

		peer = get_down_timeout_peer(now);

		if (peer != NULL) {
//			TSDebug("balancer","down timeout target is not NULL !  target id-> %d now-> %ld checked-> %ld down-> %d ",
//					peer->id, now, peer->checked, peer->down);
			return *peer;
		}

		if (this->peersS_number == OS_SINGLE) {
			if (this->targets_s[0].down) {
				goto failed;
			}
			return this->targets_s[0];
		} else {
//			TSDebug("balancer", "go get_healthy_peer main targets !");
			peer = get_healthy_peer(targets_s, now);
			if (peer == NULL) {
				goto failed;
			}
			return *peer;
		}

	failed:
		if (!targets_b.empty()) {
//			TSDebug("balancer", "backup targets is not NULL !");
			if (peersB_number == OS_SINGLE) {
				if (targets_b[0].down) {
					goto clear_fails;
				}
				return targets_b[0];
			} else {
//				TSDebug("balancer", "go get_healthy_peer backup targets !");
				peer = get_healthy_peer(targets_b, now);
				if (peer == NULL) {
					goto clear_fails;
				}
				return *peer;
			}
		}

	clear_fails:
		clean_peer_status();
		//当所有服务都down的时候，进入轮询模式,(主备都需要轮询,尽快找出健康的os)
		//该状态下的target 都不会回源（除了hit_stale）
		++next;
		next = (next == UINT64_MAX ? 0 : next);
		if (peersB_number && (next % 2)) {    //主备选择
			return this->targets_b[next % this->targets_b.size()];
		}

		//防止主不存在
		if(this->peersS_number)
			return this->targets_s[next % this->targets_s.size()];
		else
			return this->targets_b[next % this->targets_b.size()];
	}

	//清除peer 的fails 和 timeout_fails状态
	void clean_peer_status() {
		uint i;
		size_t t_len;

		t_len = targets_s.size();
		for (i = 0; i < t_len; i++) {
			targets_s[i].fails = 0;
			targets_s[i].timeout_fails = 1;
		}

		t_len = targets_b.size();
		for (i = 0; i < t_len; i++) {
			targets_b[i].fails = 0;
			targets_b[i].timeout_fails = 1;
		}
	}

	//首先给down状态下的服务器一次机会 now - check >= fail_timeout * timeout_fails
	//如果主还有存活的，就不用考虑down状态下冷却超时的备用，只有当主都不存活，才考虑
	BalancerTarget *get_down_timeout_peer(time_t now) {
		uint i;
		size_t t_len;
		BalancerTarget *peer, *check_peer;
		peer = NULL;
		check_peer = NULL;

		t_len = targets_s.size();
		for (i = 0; i < t_len; i++) {
			peer = &(targets_s[i]);
			if(!peer->down)
				return NULL;
			if (peer->down && (now - peer->checked) > (peer->timeout_fails * peer->fail_timeout)) {
				peer->checked = now;
				check_peer = peer;
			}
		}
		if (check_peer)
			return check_peer;

		peer = NULL;
		check_peer = NULL;
		t_len = targets_b.size();
		for (i = 0; i < t_len; i++) {
			peer = &(targets_b[i]);
			if(!peer->down)
				return NULL;
			if (peer->down && (now - peer->checked) > (peer->timeout_fails * peer->fail_timeout)) {
				peer->checked = now;
				check_peer = peer;
			}
		}

		return check_peer;
	}

	bool is_roundrobin_balancer() {
		return this->is_balancer;
	}

	//获取最优的target 此处参考nginx rr 算法
	BalancerTarget *
	get_healthy_peer(std::vector<BalancerTarget> &targets, time_t now) {
		BalancerTarget *best, *peer;
		int total;
		uint i;

		best = NULL;
		peer = NULL;
		total = 0;

		size_t t_len = targets.size();

		for (i = 0; i < t_len; i++) {

			peer = &(targets[i]);
			if (peer->down) {
				continue;
			}
			//如果在fail_timeout内 失败次数fails >= max_fails 不可取
			if (peer->max_fails && peer->fails >= peer->max_fails
					&& now - peer->checked <= peer->fail_timeout) {
				continue;
			}

			peer->current_weight += peer->effective_weight;
			total += peer->effective_weight;

			if (peer->effective_weight < int(peer->weight)) {
				peer->effective_weight++;
			}

			if (best == NULL || peer->current_weight > best->current_weight) {
				best = peer;
			}
		}

		if (best == NULL) {
			return NULL;
		}

		best->current_weight -= total;

		if (now - best->checked > best->fail_timeout) {
			best->checked = now;
		}

		return best;
	}

	//更改后端状态,后端返回5xx，就认为失败
	TSReturnCode os_response_back_status(uint target_id, TSHttpStatus status) {
//		TSDebug("balancer"," os_response_back_status => target_id -> %d, status -> %d ",target_id, status);
		BalancerTarget *peer;
		size_t t_len;
		uint i;
		time_t now;

		peer = NULL;
		t_len = 0;

		if (!targets_s.empty())
			t_len = targets_s.size();
		for (i = 0; i < t_len; i++) {
			if (targets_s[i].id == target_id) {
				peer = &(targets_s[i]);
				break;
			}
		}
		if (peer == NULL && !targets_b.empty()) {
			t_len = targets_b.size();
			for (i = 0; i < t_len; i++) {
				if (targets_b[i].id == target_id) {
					peer = &(targets_b[i]);
					break;
				}
			}
		}

		if (peer == NULL)
			return TS_SUCCESS;

//		TSDebug("balancer", "os_response_back_status check time %ld accessed time %ld! ",	peer->checked, peer->accessed);

		if (status >= FAIL_STATUS) {
			now = TShrtime() / TS_HRTIME_SECOND;
			peer->checked = now;
			peer->accessed = now;
			if (peer->down) {
				peer->timeout_fails++;
				peer->timeout_fails = peer->timeout_fails > MAX_FAIL_TIME ? MAX_FAIL_TIME : peer->timeout_fails;
//				TSDebug("balancer", " os_response_back_status  target id-> %d is down again timeout_fails-> %d ",
//						peer->id, peer->timeout_fails);

			} else {
				peer->fails++;
				if (peer->max_fails) {
					peer->effective_weight -= peer->weight / peer->max_fails;
				}

				if (peer->fails >= peer->max_fails) {
					peer->down = 1;
					peer->timeout_fails = 1;
//					TSDebug("balancer", " os_response_back_status  target id-> %d is down ", peer->id);
				}
			}

			if (peer->effective_weight < 0) {
				peer->effective_weight = 0;
			}

		} else {

			if (peer->accessed < peer->checked) {
				peer->fails = 0;
			}

			//如果有一次探测正常，就将timeout_fail--, 直到为1，则将该后端服务down状态去掉,后续可以优化一下
			if (peer->down) {			  //可以不用防止并发的情况
				if (peer->timeout_fails <= 1) {
					peer->down = 0;
					peer->timeout_fails = 1;
					peer->fails = 0;
					peer->effective_weight = peer->weight;
					peer->current_weight = 0;
					peer->accessed = 0;
					peer->checked = 0;
				} else {
					//当服务器状态从坏到好的时候，下降的基数稍微大点
					now = TShrtime() / TS_HRTIME_SECOND;
					peer->timeout_fails = peer->timeout_fails / 2;
					peer->timeout_fails = peer->timeout_fails ? peer->timeout_fails : 1;
					peer->checked = now;
					peer->accessed = now; //因为peer 状态还是down ，所以这里accessed 还需要赋值
				}
//				TSDebug("balancer", " os_response_back_status target is down but return is OK, target->id %d", peer->id);
			}

		}
		peer = NULL;
		return TS_SUCCESS;
	}

	const char *get_path() {
		return this->path;
	}

	std::vector<BalancerTarget> targets_s; //主线路

	std::vector<BalancerTarget> targets_b; //备用线路
	uint peersS_number;
	uint peersB_number;
	unsigned next;
	char *path;bool is_balancer;
};

} // namespace

BalancerInstance *
MakeRoundRobinBalancer(const char *options) {
	RoundRobinBalancer *hash = new RoundRobinBalancer();
//  char *opt;
//  char *tmp;

	if (options) {
		if (strchr(options, ',')) {
			TSError("[balancer] Ignoring invalid round robin field '%s'", options);
		}

		hash->path = strdup(options);
		TSDebug("balancer","making round robin balancer with path options '%s'", hash->path);
//    options = tmp = strdup(options);
//    while ((opt = strsep(&tmp, ",")) != NULL) {
//
//      TSError("[balancer] Ignoring invalid round robin field '%s'", opt);
//    }
//
//    free((void *)options);
	}

	return hash;
}
