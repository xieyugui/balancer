/*
 * roundrobin.h
 *
 *  Created on: 2016年5月6日
 *      Author: xie
 */

#ifndef BALANCER_ROUNDROBIN_H
#define BALANCER_ROUNDROBIN_H

#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "balancer.h"


class RoundRobinBalancer : public BalancerInstance {

public:
	RoundRobinBalancer(BalancerType b_type);
	~RoundRobinBalancer();

	void hold() {
		ink_atomic_increment(&_ref_count, 1);
//	   TSDebug(PLUGIN_NAME,"----------hold  _ref_count---------------%d",_ref_count);
	}

	void release() {
		if (1 >= ink_atomic_decrement(&_ref_count, 1)) {
//		   TSDebug(PLUGIN_NAME,"----------release  _ref_count---------------%d",_ref_count);
			delete this;
		}

	}

	void push_target(BalancerTarget *target);

	//获取一个后端
	BalancerTarget *balance(TSHttpTxn, TSRemapRequestInfo *);

	//清除peer 的fails 和 timeout_fails状态
	void clean_peer_status();

	//首先给down状态下的服务器一次机会 now - check >= fail_timeout * timeout_fails
	//如果主还有存活的，就不用考虑down状态下冷却超时的备用，只有当主都不存活，才考虑
	BalancerTarget *get_down_timeout_peer(time_t now);

	//获取最优的target 此处参考nginx rr 算法
	BalancerTarget *get_healthy_peer(std::vector<BalancerTarget *> &targets, time_t now);

	//更改后端状态,后端返回5xx，就认为失败
	TSReturnCode os_response_back_status(uint target_id, TSHttpStatus status);

	BalancerTarget * make_balancer_target(const char *strval);

	void set_path(char *path) {
		this->path = path;
	}

	char *get_path() const {
		return this->path;
	}

	void set_backend_tag(bool is_need, bool is_need_health_check) {
		this->need_https_backend = is_need;
		this->need_health_check = is_need_health_check;
	}

	bool get_https_backend_tag() {
		return this->need_https_backend;
	}

	bool get_health_check_tag() {
		return this->need_health_check;
	}

	BalancerType get_balancer_type() { return this->b_type; }

private:
	std::vector<BalancerTarget *> targets_s; //主线路

	std::vector<BalancerTarget *> targets_b; //备用线路
	uint peersS_number;
	uint peersB_number;
	unsigned next;
	char *path;
	bool need_https_backend;
	bool need_health_check;
    BalancerType b_type;
	volatile int _ref_count;
};

BalancerInstance *
MakeRoundRobinBalancer(const char *options, BalancerType b_type)
{
	RoundRobinBalancer *balancer = new RoundRobinBalancer(b_type);
//	char *opt;
//	char *tmp;

	TSDebug("balancer", "making round robin balancer with options '%s'", options);

//	if (options) {
//		options = tmp = strdup(options);
//		while ((opt = strsep(&tmp, ",")) != nullptr) {
//			TSError("[balancer] Ignoring invalid round robin field '%s'", opt);
//		}
//
//		free((void *)options);
//	}

    balancer->hold();
//    const char *options = end ? end + 1 : NULL;
    if (options) {
        if (strchr(options, ',')) {
            TSError("[%s] Ignoring invalid round robin field '%s'", PLUGIN_NAME, options);
        }
        balancer->set_path(strdup(options));
        free((void *)options);
    }

	return balancer;
}


#endif /* BALANCER_ROUNDROBIN_H */
