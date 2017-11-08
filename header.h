/*
* @author:    daemon.xie
* @license:   Apache Licence
* @contact:   xieyugui@migu.cn 
* @software:  CLion
* @file:      header.h
* @date:      2017/11/7 下午2:54
* @desc:
*/

//
// Created by daemon.xie on 2017/11/7.
//

#ifndef BALANCER_HEADER_H
#define BALANCER_HEADER_H

#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "balancer.h"

class HeaderBalancer : public BalancerInstance {

public:
    HeaderBalancer(BalancerType b_type);
    ~HeaderBalancer();

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

    void push_target(BalancerTarget *target)  {
        this->target = target;
    }

    //获取一个后端
    BalancerTarget *balance(TSHttpTxn, TSRemapRequestInfo *){
        return this->target;
    }

    void clean_peer_status() {}

    BalancerTarget *get_down_timeout_peer(time_t now) { return nullptr; }

    BalancerTarget *get_healthy_peer(std::vector<BalancerTarget *> &targets, time_t now) { return nullptr;}

    TSReturnCode os_response_back_status(uint target_id, TSHttpStatus status) {return TS_SUCCESS;}

    BalancerTarget * make_balancer_target(const char *strval);

    void set_path(char *path) {}

    char *get_path() const {
        return nullptr;
    }

    void set_backend_tag(bool is_need, bool is_need_health_check) {
        this->need_https_backend = is_need;
    }

    bool get_https_backend_tag() {
        return this->need_https_backend;
    }

    bool get_health_check_tag() {
        return false;
    }

    BalancerType get_balancer_type() { return this->b_type; }

private:
    BalancerTarget *target;

    bool need_https_backend;
    BalancerType b_type;
    volatile int _ref_count;
};

BalancerInstance *
MakeHeaderBalancer(const char *options, BalancerType b_type)
{
    HeaderBalancer *header = new HeaderBalancer(b_type);

    TSDebug("balancer", "making header balancer with options '%s'", options);

    if (options) {
//        options = tmp = strdup(options);
//        while ((opt = strsep(&tmp, ",")) != nullptr) {
//            TSError("[balancer] Ignoring invalid round robin field '%s'", opt);
//        }

        free((void *)options);
    }

    return header;
}


#endif //BALANCER_HEADER_H
