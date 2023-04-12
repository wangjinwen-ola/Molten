mo_log_write
mo_chain_log_flush
mo_rep_record_data

php -d extension=molten.so -d molten.enable=1 -d molten.sink_type=2 -d molten.tracing_cli=1 -d molten.sampling_rate=1 -d molten.span_format=zipkin -d molten.service_name='sre-test-zipkin' -r '$c=curl_init("http://localhost:12345");curl_exec($c);'

nohup tail -n0 -F tracing-*.log| xargs -d '\n' -I % sudo curl -sS -X POST -H "Content-Type: application/json" -d '%' http://172.19.21.21:9411/api/v1/spans >> trace.log 2>&1 &

➜  conf.d cat molten.ini
[molten]
extension="/usr/lib/php/20170718/molten.so"
molten.enable="1"
molten.service_name="sre-test"
molten.sampling_type=2
molten.sampling_rate=1
molten.sink_type=1
molten.span_format=zipkin
molten.sink_log_path="/data/fpm/"


#!/bin/bash

# 设置json文件路径
LOG_FILE="/data/fpm/tracing-*.log"

# 监听json文件变化，并解析json数据
tail -n 0 -F ${LOG_FILE} | while read LINE; do
    # 读取json文件中的数据
    DATA=$(echo "${LINE}")

    # 发送post请求
    sudo curl -sS -X POST -H "Content-Type: application/json" http://172.19.21.21:9411/api/v1/spans -d "${DATA}" 
done