#include <string>
#include <simdjson.h>

#include "all.h"
#include "remote_action.h"
#include "pzmq.hpp"
#include "json.hpp"
#include "StackFlowUtil.h"

using namespace StackFlows;

/**
 * 这个 remote_call 函数是一个远程过程调用（RPC）的客户端实现，主要功能是：

    核心流程

    解析JSON请求：
    使用 simdjson 解析输入的 JSON 字符串
    提取 work_id 和 action 字段

    提取工作单元：
    从 work_id 中提取工作单元名（如 "test.123" → "test"）

    构建通信URL：
    使用 zmq_c_format 格式和 com_id 生成 ZMQ 通信地址

    执行远程调用：
    创建 pzmq 客户端连接到对应的工作单元
    调用远程的 action 方法，传递通信URL和原始JSON数据
 */

 /**
  * com_id: 通信ID，用于生成ZMQ连接地址
  * json_str: JSON格式的请求数据，包含work_id和action
  */
int remote_call(int com_id, const std::string &json_str) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string json_string(json_str);
    simdjson::ondemand::document doc;
    auto error = parser.iterate(json_string).get(doc);

    std::string work_id;
    doc["work_id"].get_string(work_id);
    std::string work_unit = work_id.substr(0, work_id.find("."));
    std::string action;
    doc["action"].get_string(action);

    if (work_id.empty() || action.empty()) {
        throw std::runtime_error("Invalid JSON: missing work_id or action");
    }
    char com_url[256];

    /**
     * 实际例子
        假设：

        zmq_c_format = "tcp://localhost:%d"
        com_id = 5001
        执行后：

        com_url 变成 "tcp://localhost:5001"
     */

    /**
     * 255 是缓冲区的最大写入长度。

        作用
        防止写入的字符串超过  com_url 数组的大小
        避免缓冲区溢出（buffer overflow

         com_url 数组大小是 256 字节
        snprintf 最多写入 255 个字符
        留1个字节给字符串结束符 \0
     */
    snprintf(com_url, 255, zmq_c_format.c_str(), com_id);
    pzmq clent(work_unit);

    // 打包操作:客户端相关url数据
    return clent.call_rpc_action(action, pzmq_data::set_param(com_url, json_str),
                                [](pzmq *_pzmq, const std::shared_ptr<pzmq_data> &val) {});
}