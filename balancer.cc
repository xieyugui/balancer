/** @file

 A brief file description

 @section license License

 Licensed to the Apache Software Foundation (ASF) under one
 or more contributor license agreements.  See the NOTICE file
 distributed with this work for additional information
 regarding copyright ownership.  The ASF licenses this file
 to you under the Apache License, Version 2.0 (the
 "License"); you may not use this file except in compliance
 with the License.  You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#include "balancer.h"
#include <ts/remap.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <iterator>

// Using ink_inet API is cheating, but I was too lazy to write new IPv6 address parsing routines ;)
#include "ts/ink_inet.h"

#define PLUGIN_NAME "balancer"

// The policy type is the first comma-separated token.
static BalancerInstance *
MakeBalancerInstance(const char *opt) {
	const char *end = strchr(opt, ',');
	size_t len = end ? std::distance(opt, end) : strlen(opt);

	if (len == lengthof("hash") && strncmp(opt, "hash", len) == 0) {
		return MakeHashBalancer(end ? end + 1 : NULL);
	} else if (len == lengthof("roundrobin")
			&& strncmp(opt, "roundrobin", len) == 0) {
		return MakeRoundRobinBalancer(end ? end + 1 : NULL);
	} else {
		TSError("[balancer] Invalid balancing policy '%.*s'", (int) len, opt);
		return NULL;
	}
}

static BalancerTarget MakeBalancerTarget(const char *strval) {
	BalancerTarget target = BalancerTarget();

	target.weight = 1;
	target.effective_weight = 1;
	target.current_weight = 0;
	target.max_fails = 1;
	target.fail_timeout = 10;
	target.down = 0;
	target.backup = 0;
	target.fails = 0;
	target.accessed = 0;
	target.checked = 0;
	target.timeout_fails = 1;

	union {
		struct sockaddr_storage storage;
		struct sockaddr sa;
	} address;

	memset(&address, 0, sizeof(address));

	// First, check whether we have an address literal.
	TSDebug("balancer", "start check argv");
	const char *is_address_literal = strrchr(strval, ',');
	if ( NULL == is_address_literal && ats_ip_pton(strval, &address.sa) == 0) {
		char namebuf[INET6_ADDRSTRLEN];

		target.port = ats_ip_port_host_order(&address.sa);
		target.name = ats_ip_ntop(&address.sa, namebuf, sizeof(namebuf));

	} else {
		//modify by daemon.xie
		//格式ip:port,是否为备用线路,权重,最大失败次数,禁用时间
		// 192.168.8.7:80,0,1,1,10   如果只有ip 后面几个参数都是默认值
		int target_array[4] = { 0, 1, 1, 10 };
		uint a_count = sizeof(target_array) / sizeof(target_array[0]);
		uint s_count = 0;
		const char *comma = strrchr(strval, ':');
		if (comma) {
			target.name = std::string(strval, (comma - strval));
			target.port = strtol(comma + 1, NULL, 10);

			comma = strchr(comma + 1, ',');
			while ( NULL != comma && s_count <= a_count) {
				target_array[s_count] = strtol(comma + 1, NULL, 10);
				s_count += 1;
				comma = strchr(comma + 1, ',');
			}
		} else {
			comma = strchr(strval, ',');
			if (comma) {
				target.name = std::string(strval, (comma - strval));
				while ( NULL != comma && s_count <= a_count) {
					target_array[s_count] = strtol(comma + 1, NULL, 10);
					s_count += 1;
					comma = strchr(comma + 1, ',');
				}
			} else {
				target.name = strval;
			}
		}
		target.backup = target_array[0];
		target.weight = target_array[1];
		target.max_fails = target_array[2];
		target.fail_timeout = target_array[3];
	}

	if (target.port > INT16_MAX) {
		TSError("[balancer] Ignoring invalid port number for target '%s'",
				strval);
		target.port = 0;
	}

	// add by daemon.xie
	TSDebug("balancer",
			"balancer target -> %s  target.name -> %s target.port -> %d target.backup ->%d target.weight -> %d target.max_fails ->%d target.fail_timeout -> %ld",
			strval, target.name.c_str(), target.port, target.backup,
			target.weight, target.max_fails, target.fail_timeout);

	return target;
}

TSReturnCode TSRemapInit(TSRemapInterface * /* api */, char * /* errbuf */,
		int /* bufsz */) {
	return TS_SUCCESS;
}

static TSReturnCode send_response_handle(TSHttpTxn txnp,
		BalancerTargetStatus *targetstatus) {
	TSMBuffer response;
	TSMLoc resp_hdr;
	TSHttpStatus status;

	TSMBuffer bufp;
	TSMLoc hdr_loc;
	char *buf;

	if ( NULL == targetstatus && targetstatus->binstance) {
		return TS_ERROR;
	}

	if (targetstatus->object_status == TS_CACHE_LOOKUP_HIT_FRESH) {
		return TS_SUCCESS;
	}

	TSDebug("balancer", "Starting send_response_handle");

	//if (targetstatus->target_id >= 0 && targetstatus->target_down == 0) {
	//回源check 包括down check
	if ( targetstatus->target_id >= 0  && (!targetstatus->target_down or (targetstatus->target_down && targetstatus->is_down_check) )) {
		//当源站没有正常返回的情况下，都会返回ts_error
		status = TS_HTTP_STATUS_BAD_GATEWAY;
		//TODO 如果是回源304 check 的情况该如何处理？
		//当前的ats ，当文件过期，正好源站不通的时候，返回旧文件，当源站有任务返回的时候，ats 将会返回该内容
		//TSHttpTxnServerRespNoStoreSet(txn, 1);
		if (TSHttpTxnServerRespGet(txnp, &response, &resp_hdr) == TS_SUCCESS) {
			status = TSHttpHdrStatusGet(response, resp_hdr);
			TSHandleMLocRelease(response, TS_NULL_MLOC, resp_hdr);
		}

		TSDebug("balancer", "handle_response (): Get status %d, do something.",
				status);
		targetstatus->binstance->os_response_back_status(
				targetstatus->target_id, status);


	} else if(targetstatus->object_status != TS_CACHE_LOOKUP_HIT_STALE){//排除hit_stale情况, 走ats 默认配置流程，返回源站信息or 返回旧数据
		if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) == TS_ERROR) {
			return TS_ERROR;
		}

		TSDebug("balancer", " target.id == -1 or target_down  == 1!");
		TSHttpHdrStatusSet(bufp, hdr_loc, TS_HTTP_STATUS_BAD_GATEWAY);
		TSHttpHdrReasonSet(bufp, hdr_loc,
				TSHttpHdrReasonLookup(TS_HTTP_STATUS_BAD_GATEWAY),
				strlen(TSHttpHdrReasonLookup(TS_HTTP_STATUS_BAD_GATEWAY)));

		buf = (char *) TSmalloc(100);

		sprintf(buf, "502 Source station temporarily unavailable!\n");

		TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

		//自己会释放点buf,不需要TSfree?
		TSHttpTxnErrorBodySet(txnp, buf, strlen(buf), NULL);
	}

	TSDebug("balancer", "end get response header!");

	return TS_SUCCESS;
}

//如果命中
static TSReturnCode look_up_handle (TSCont contp, TSHttpTxn txnp, BalancerTargetStatus *targetstatus) {

	int obj_status;
	TSDebug("balancer", "start look_up_handle");
	if ( NULL == targetstatus && targetstatus->binstance) {
		return TS_ERROR;
	}

	 if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
	   TSError("[%s] Couldn't get cache status of object", __FUNCTION__);
	    return TS_ERROR;
	 }
	 targetstatus->object_status = obj_status;
	 //排除 hit_fresh 的情况，不需要回源
	 if (obj_status == TS_CACHE_LOOKUP_HIT_FRESH ) {
		 return TS_ERROR;
	 }

	 TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
	 TSDebug("balancer", "add TS_HTTP_SEND_RESPONSE_HDR_HOOK");
	 //if (obj_status != TS_CACHE_LOOKUP_HIT_STALE && obj_status != TS_CACHE_LOOKUP_HIT_FRESH) {
	 //hit stale 情况 主要是考虑，当文件过期的时候，还可以返回过期文件，而不是直接返回502
     if (obj_status != TS_CACHE_LOOKUP_HIT_STALE) {
		TSDebug("balancer", "obj_status != TS_CACHE_LOOKUP_HIT_STALE");
		if (targetstatus->target_down && !targetstatus->is_down_check)
			return TS_SUCCESS;
	 }
	 TSDebug("balancer", "end look_up_handle");
	 return TS_ERROR;
}

/**
 * Transaction event handler.
 */
static void balancer_handler(TSCont contp, TSEvent event, void *edata) {
	TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
	BalancerTargetStatus *targetstatus;
	targetstatus = (struct BalancerTargetStatus *) TSContDataGet(contp);

	TSEvent reenable = TS_EVENT_HTTP_CONTINUE;

	switch (event) {
//  case TS_EVENT_HTTP_READ_RESPONSE_HDR:  当源站不通的时候，不触发该事件
//    handle_server_read_response(txnp, targetstatus);
//    break;
	case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
		if (look_up_handle(contp, txnp, targetstatus) == TS_SUCCESS) {
			TSDebug("balancer", "change to TS_EVENT_HTTP_ERROR");
			reenable = TS_EVENT_HTTP_ERROR;
		}
		break;
	case TS_EVENT_HTTP_SEND_RESPONSE_HDR://放在lookup 里添加
		send_response_handle(txnp, targetstatus);
		break;
	case TS_EVENT_HTTP_TXN_CLOSE:
		if (targetstatus) {
			TSfree(targetstatus);
		}
		TSContDestroy(contp);
		break;
	default:
		break;
	}
	TSHttpTxnReenable(txnp, reenable);
}

///////////////////////////////////////////////////////////////////////////////
// One instance per remap.config invocation.
//
TSReturnCode TSRemapNewInstance(int argc, char *argv[], void **instance,
		char *errbuf, int errbuf_size) {
	static const struct option longopt[] = { { const_cast<char *>("policy"),
			required_argument, 0, 'p' }, { 0, 0, 0, 0 } };

	BalancerInstance *balancer = NULL;

	// The first two arguments are the "from" and "to" URL string. We need to
	// skip them, but we also require that there be an option to masquerade as
	// argv[0], so we increment the argument indexes by 1 rather than by 2.
	argc--;
	argv++;

	optind = 0;
	for (;;) {
		int opt;

		opt = getopt_long(argc, (char * const *) argv, "", longopt, NULL);
		switch (opt) {
		case 'p':
			balancer = MakeBalancerInstance(optarg);
			break;
		case -1:
			break;
		default:
			snprintf(errbuf, errbuf_size, "invalid balancer option '%d'", opt);
			delete balancer;
			return TS_ERROR;
		}

		if (opt == -1) {
			break;
		}
	}

	if (!balancer) {
		strncpy(errbuf, "missing balancer policy", errbuf_size);
		return TS_ERROR;
	}

	// Pick up the remaining options as balance targets.
	uint s_count = 0;
	int i;
	for (i = optind; i < argc; ++i) {
		BalancerTarget target = MakeBalancerTarget(argv[i]);
		target.id = s_count;
		s_count ++;
		balancer->push_target(target);
		if (target.port) {
			TSDebug("balancer", "added target -> %s:%u", target.name.c_str(),
					target.port);
		} else {
			TSDebug("balancer", "added target -> %s", target.name.c_str());
		}
	}

	*instance = balancer;
	return TS_SUCCESS;
}

void TSRemapDeleteInstance(void *instance) {
	delete (BalancerInstance *) instance;
}

TSRemapStatus TSRemapDoRemap(void *instance, TSHttpTxn txn,
		TSRemapRequestInfo *rri) {
	TSCont txn_contp;
	BalancerInstance *balancer = (BalancerInstance *) instance;
	const BalancerTarget &target = balancer->balance(txn, rri);

	TSUrlHostSet(rri->requestBufp, rri->requestUrl, target.name.data(),
				target.name.size());

	if (target.port) {
		TSUrlPortSet(rri->requestBufp, rri->requestUrl, target.port);
	}

	if (balancer->is_roundrobin_balancer()) {

		TSDebug("balancer", "the balancer is a roundrobin balancer!");
		BalancerTargetStatus *targetstatus;
		targetstatus = (BalancerTargetStatus *) TSmalloc(
				sizeof(BalancerTargetStatus));
		targetstatus->binstance = balancer;
		targetstatus->target_id = target.id;
		targetstatus->target_down = target.down;
		targetstatus->is_down_check = false;
		targetstatus->object_status = TS_CACHE_LOOKUP_MISS;
		if (target.down ) {
			time_t now = TShrtime() / TS_HRTIME_SECOND;
			if ((now - target.accessed) > (target.timeout_fails * target.fail_timeout)) {
				TSDebug("balancer", "is down check accessed %ld! ",target.accessed);
				targetstatus->is_down_check = true;
			}
		}
		if (NULL == (txn_contp = TSContCreate((TSEventFunc) balancer_handler, NULL))) {
			TSError("[%s] TSContCreate(): failed to create the transaction handler continuation.", PLUGIN_NAME);
			if (targetstatus)
				TSfree(targetstatus);
		} else {
			TSContDataSet(txn_contp, targetstatus);

			TSHttpTxnHookAdd(txn, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, txn_contp);
			TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);

		}

	}

	return TSREMAP_DID_REMAP;
}
