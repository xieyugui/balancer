# balancer
#### Docs: 
     https://docs.trafficserver.apache.org/en/5.3.x/reference/plugins/balancer.en.html 

#### Project address: 
     https://github.com/xieyugui/balancer.git

#### Manual:
	在roundrobin 模式下新增 backup、weight、max_fails、fail_timeout ,以及add path和开启https回源的功能; 
	当添加--open参数时, backup、weight、max_fails、fail_timeout等参数才会生效，不然只有默认轮询功能;
	当添加--follow参数时，则启用回源跟随功能，参数回源地址后后面跟(,0/1 #0--http, 1--https回源)，目前只支持单个地址回源模式，不支持轮询以及健康检测功能,
	    注意：开启follow功能时，会检测头X-Forwarded-Proto 的value，默认为空/http，如果value=https，就按照https源地址回源

##### backup=number
    marks the server as a backup server. It will be passed requests when the primary servers are 
    unavailable.by default, 0    --(0/1)

##### weight=number
    sets the weight of the server, by default, 1.

##### max_fails=number
    sets the number of unsuccessful attempts to communicate with the server that should happen 
    in the duration set by the fail_timeout parameter to consider the server unavailable for 
    a duration also set by the fail_timeout parameter. By default, the number of unsuccessful
    attempts is set to 3. The zero value disables the accounting of attempts. What is considered
    an unsuccessful attempt is defined by the proxy_next_upstream, fastcgi_next_upstream, 
    uwsgi_next_upstream, scgi_next_upstream, and memcached_next_upstream directives.

##### fail_timeout=time
    sets the time during which the specified number of unsuccessful attempts to communicate 
    with the server should happen to consider the server unavailable; and the period of time 
    the server will be considered unavailable. By default, the parameter is set to 10 seconds.
    
### For example:
    map http://foo.com http://foo.com  @plugin=balancer.so @pparam=--policy=roundrobin @pparam=--https @pparam=--open @pparam=one.bar.com:80,0,1,5,30 @pparam=two.bar.com,0,1,5,30
    map http://foo.com http://foo.com  @plugin=balancer.so @pparam=--policy=roundrobin @pparam=--https @pparam=--open @pparam=one.bar.com:80,0,1,5,30 @pparam=two.bar.com,1,1,5,30
    map http://foo.com http://foo.com  @plugin=balancer.so @pparam=--policy=roundrobin @pparam=--follow @pparam=one.bar.com:80,0 @pparam=two.bar.com,1
 
### Add path:
    map http://foo.com http://foo.com  @plugin=balancer.so @pparam=--policy=roundrobin,0/ @pparam=--open @pparam=one.bar.com:80,0,1,5,30 @pparam=two.bar.com,0,1,5,30 <br />
    if client request http://cdnxxx.com/1.jpg then ATS will back to the source server request http://cdnxxx.com/0/1.jpg

# 未完成
