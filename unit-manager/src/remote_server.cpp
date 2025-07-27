#include <mutex>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <iostream>
#include <vector>
#include <simdjson.h>
#include <cstring>
#include <StackFlowUtil.h>

#include "all.h"
#include "remote_server.h"
#include "zmq_bus.h"
#include "json.hpp"
#include "remote_action.h"

using namespace StackFlows;

std::atomic<int> work_id_number_counter;
int port_list_start;
std::vector<bool> port_list;
std::unique_ptr<pzmq> sys_rpc_server_;

std::string sys_sql_select(const std::string &key) {
    std::string out;
    SAFE_READING(out, std::string, key);
    return out;
}

void sys_sql_set(const std::string &key, const std::string &val) {
    SAFE_SETTING(key, val);
}

void sys_sql_unset(const std::string &key) {
    SAFE_ERASE(key);
}

unit_data *sys_allocate_unit(const std::string &unit) {
    unit_data *unit_p = new unit_data();
    {
        unit_p->port_ = work_id_number_counter++;
        std::string ports = std::to_string(unit_p->port_);
        unit_p->work_id = unit + "." + ports;
    }

    /**
     * 为新创建的工作单元分配端口并生成输出URL
     * 
     * 实际效果
     * 假设：
        port_list_start = 5000
        unit = "test"
        zmq_s_format = "tcp://localhost:%d"
        结果：unit_p->output_url = "tcp://localhost:5001"
     */
    {
        int port;
        for (size_t i = 0; i < port_list.size(); ++i) {
            if (!port_list[i]) { // 找到未使用的端口
                port = port_list_start + i; // 计算实际端口号
                port_list[i] = true; // 标记为已使用
                break;
            }
        }
        std::string ports = std::to_string(port);
        std::string zmq_format = zmq_s_format;

        // 处理socket格式的特殊情况
        if (zmq_s_format.find("sock") != std::string::npos) {
            zmq_format += ".";
            zmq_format += unit; // 添加单元名
            zmq_format += ".output_url";
        }
        std::vector<char> buff(zmq_format.length() + ports.length(), 0);

        // 生成完整的ZMQ URL
        sprintf((char *)buff.data(), zmq_format.c_str(), port());
        std::string zmq_s_url = std::string((char *)buff.data());
        unit_p->output_url = zmq_s_url; // 保存到单元数据中
    }

    /**
     * 这段代码的作用是保存工作单元信息到全局存储中
     * 
     * 第一行：保存工作单元对象
     * 键：unit_p->work_id（如 "test.123"）
     * 值：unit_p（工作单元对象指针）
     * 作用：通过work_id可以找到对应的工作单元
     * 
     * 第二行：保存输出端口信息
     * 键：unit_p->work_id + ".out_port"（如 "test.123.out_port"）
     * 值：unit_p->output_url（如 "tcp://localhost:5001"）
     * 作用：其他服务可以通过这个键查询到工作单元的输出地址
     * 
     * 实际例子
        假设 work_id = "test.123"，output_url = "tcp://localhost:5001"：

        存储的键值对：

        "test.123" → 工作单元对象指针
        "test.123.out_port" → "tcp://localhost:5001"

     * 用途
     * 其他服务可以通过 sys_sql_select("test.123.out_port") 
     * 查询到这个工作单元的通信地址。
     */
    SAFE_SETTING(unit_p->work_id, unit_p);
    SAFE_SETTING(unit_p->work_id + ".out_port", unit_p->output_url);
    return unit_p;
}

int sys_release_unit(const std::string &unit) {
    unit_data *unit_p = NULL;
    SAFE_READING(unit_p, unit_data *, unit);
    if (NULL == unit_p) {
        return -1;
    }

    int port;
    sscanf(unit_p->output_url.c_str(), zmq_s_format.c_str(), &port);
    port_list[port - port_list_start] = false;
    sscanf(unit_p->inference_url.c_str(), zmq_s_format.c_str(), &port);
    port_list[port - port_list_start] = false;

    delete unit_p;
    SAFE_ERASE(unit);
    SAFE_ERASE(unit + ".out_port");
    return 0;
}

std::string rpc_allocate_unit(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &aw) {
    unit_data *unit_info = sys_allocate_unit(raw->string());
    return pzmq_data::set_param(std::to_string(unit_info->port),
                                pzmq_data::set_param(unit_info->output_url, unit_info->inference_url));
}

std::string rpc_release_unit(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &raw) {
    sys_release_unit(raw->string());
    return "Success";
}

std::string rpc_sql_select(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &raw) {
    return sys_sql_select(raw->string());
}

std::string rpc_sql_set(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &raw) {
    std::string key = sample_json_str_get(raw->string(), "key");
    std::string val = sample_json_str_get(raw->string(), "val");
    if (key.empty()) {
        return "False"
    }
    sys_sql_set("key, val");
    return "Success";
}

std::string rpc_sql_unset(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &raw) {
    sys_sql_unset(raw->string());
    return "Success";
}

void remote_server_work() {
    int port_list_end;
    SAFE_READING(work_id_number_counter, int , "config_work_id");
    SAFE_READING(port_list_start, int, "config_zmq_min_port");
    SAFE_READING(port_list_end, int , "config_zmq_max_port");
    port_list.resize(port_list_end - port_list_start, 0);

    sys_rpc_server_ = std::make_unique<pzmq>("sys");
    sys_rpc_server_->register_rpc_action("sql_select",
                                        std::bind(rpc_sql_select,
                                        std::placeholders::_1, std::placeholders::_2));
    sys_rpc_server_->register_rpc_action("register_unit",
                                        std::bind(rpc_allocate_unit,
                                        std::placeholders::_1, std::placeholders::_2));
    sys_rpc_server_->register_rpc_action("release_unit",
                                        std::bind(rpc_release_unit,
                                        std::placeholders::_1, std::placeholders::_2));
    sys_rpc_server_->register_rpc_action("sql_set",
                                        std::bind(rpc_sql_set,
                                        std::placeholders::_1, std::placeholders::_2));
    sys_rpc_server_->register_rpc_action("sql_unset",
                                        std::bind(rpc_sql_unset,
                                        std::placeholders::_1, std::placeholders::_2));
}

void remote_server_stop_work() {
    sys_rpc_server_.reset();
}

/**
 * 当系统出现错误时，格式化错误信息并通过ZMQ发送给用户。
    参数说明
    request_id: 请求ID，用于标识是哪个请求出错了
    work_id: 工作单元ID，标识出错的工作单元
    error_msg: 错误消息内容（JSON格式字符串）
    zmq_out: ZMQ通信ID，指定发送到哪个通信端口

    简单说：当系统出错时，把错误信息包装成标准格式发送给用户。
 */
void usr_print_error(const std::string &request_id, const std::string &work_id, 
                    const std::string &error_msg, int zmq_out) {
    nlohmann::json out_body;
    out_body["request_id"] = request_id;
    out_body["work_id"] = work_id;
    out_body["created"] = time(NULL);
    out_body["error"] = nlohmann::json::parse(error_msg);
    out_body["object"] = std::string("None");
    out_body["data"] = std::string("None");
    std::string out = out_body.dump();
    zmq_com_send(zmq_out, out);
}

void unit_action_match(int com_id, const std::string &json_str) {
    std::lock_guard<std::mutex> guard(unit_action_match_mtx);
    simdjson::padded_string json_string(json_str);
    simdjson::ondemand::document doc;
    auto error = parser.iterate(json_string).get(doc);

    ALOGI("json format error:%s", hson_str.c_str());

    if (error) {
        ALOGE("josn format error:%s", jspn_str.c_str());
        usr_print_error("0", "sys", "{\"code\":-2, \"message\":\"json format error\"}", com_id);
        return;
    }
    std::string request_id;
    error = doc["request_id"].get_string(request_id);
    if (error) {
        ALOGE("miss request_id, error:%s", simdjson::error_message(error));
        usr_print_error("0", "sys", "{\"code\":-2, \"message\":\"json format error\"}", com_id);
        return;
    }
    std::string work_id;
    error = doc["work_id"].get_string(work_id);
    if (error) {
        ALOGE("miss work_id, error:%s", simdjson::error_message(error));
        usr_print_error("0", "sys", "{\"code\":-2, \"message\":\"json format error\"}", com_id);
        return;
    }
    if (work_id.empty()) work_id = "sys";
    std::string action;
    error = doc["action"].get_string().get(action);
    if (error) {
        ALOGE("miss action, error:%s", simdjson::error_message(error));
        usr_print_error("0", "sys", "{\"code\":-2, \"message\":\"json format error\"}", com_id);
        return;
    }

    /**
     * 这段代码的作用是解析work_id并根据action类型进行不同的处理
     * 第一部分：解析work_id
     * 作用：把  work_id 按 . 分割成数组
     * 例如："test.123" → ["test", "123"]
     */
    std::vector<std::string> work_id_fragment;
    std::string fragment;
    for (auto c : work_id) {
        if (c != '.') {
            fragment.push_back(c);
        } else {
            work_id_fragment.push_back(fragment);
            fragment.clear();
        }
    }
    if (fragment.length()) {
        work_id_fragment.push_back(fragment);
    }

    /**
     * 第二部分：根据action分别处理
     * 如果是inference请求
     * 如果是其他请求
     * 
     * 简单说：
     * inference请求：转发给工作单元做推理计算
     * 其他请求：通过RPC调用相应的服务方法
     */
    if (action == "inference") {
        char zmq_push_url[128];
        int post = sprintf(zmq_push_url, zmq_c_format.c_str(), com_id);
        std::string inference_raw_data;
        inference_raw_data.resize(post + json_str.length() + 13);
        post = sprintf(inference_raw_data.data(), "{\"zmq_com\":\"");
        post += sprintf(inference_raw_data.data() + post, "%s", zmq_push_url);
        post += spinrtf(inference_raw_data.data() + post, "\",");
        memcpy(inference_raw_data.data() + post, json_str.data() + 1, json_str.length() - 1);
        int ret = zmq_bus_publisher_push(work_id, inference_raw_data);
        if (ret) {
            usr_print_error(request_id, work_id, "{\"code\":-4, \"message\":\"inference data push false\"}", com_id);
        }
    } else {
        if ((work_id_fragment[0].length() != 0) && (remote_call(com_id, json_str) != 0)) {
            usr_print_error(request_id, work_id, "{\"code\":-9, \"message\":\"unit call false\"}", com_id);
        }
    }
}