/*
* @author:    daemon.xie
* @license:   Apache Licence
* @contact:   xieyugui@migu.cn 
* @software:  CLion
* @file:      header.c
* @date:      2017/11/6 上午10:18
* @desc:
*/

//
// Created by daemon.xie on 2017/11/6.
//
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

#include "header.h"

HeaderBalancer::HeaderBalancer(BalancerType b_type) : _ref_count(0) {
    this->need_https_backend = false;
    this->b_type = b_type;
}

HeaderBalancer::~HeaderBalancer() {

    if(this->target) {
        delete(target);
    }
    TSDebug(PLUGIN_NAME, "----------~HeaderBalancer---------------");
}

BalancerTarget *HeaderBalancer::make_balancer_target(const char *strval) {
    BalancerTarget *target = new BalancerTarget();

    union {
        struct sockaddr_storage storage;
        struct sockaddr sa;
    } address;

    memset(&address, 0, sizeof(address));

    // First, check whether we have an address literal.
    const char *is_address_literal = strrchr(strval, ',');
    if (NULL == is_address_literal && ats_ip_pton(strval, &address.sa) == 0) {
        char namebuf[INET6_ADDRSTRLEN];

        target->port = ats_ip_port_host_order(&address.sa);
        target->name = ats_ip_ntop(&address.sa, namebuf, sizeof(namebuf));

    } else {
        //格式ip:port,是否为备用线路,权重,最大失败次数,禁用时间
        // 192.168.8.7:80,0,1,10,20   如果只有ip 后面几个参数都是默认值
        int target_array[4] = {0, 1, 10, 20};
        uint a_count = sizeof(target_array) / sizeof(target_array[0]);
        uint s_count = 0;
        const char *comma = strrchr(strval, ':');
        if (comma) {
            target->name = std::string(strval, (comma - strval));
            target->port = strtol(comma + 1, NULL, 10);

            comma = strchr(comma + 1, ',');
            while (NULL != comma && s_count <= a_count) {
                target_array[s_count] = strtol(comma + 1, NULL, 10);
                s_count += 1;
                comma = strchr(comma + 1, ',');
            }
        } else {
            comma = strchr(strval, ',');
            if (comma) {
                target->name = std::string(strval, (comma - strval));
                while (NULL != comma && s_count <= a_count) {
                    target_array[s_count] = strtol(comma + 1, NULL, 10);
                    s_count += 1;
                    comma = strchr(comma + 1, ',');
                }
            } else {
                target->name = strval;
            }
        }
        target->backup = target_array[0];
        target->weight = target_array[1];
        target->max_fails = target_array[2];
        target->fail_timeout = target_array[3];
    }

    if (target->port > INT16_MAX) {
        TSError("[%s] Ignoring invalid port number for target '%s'", PLUGIN_NAME, strval);
        target->port = 0;
    }

    TSDebug(PLUGIN_NAME,
            "balancer target -> %s  target->name -> %s target->port -> %d target->backup ->%d target->weight -> %d target->max_fails ->%d target->fail_timeout -> %ld",
            strval, target->name.c_str(), target->port, target->backup,
            target->weight, target->max_fails, target->fail_timeout);

    return target;
}