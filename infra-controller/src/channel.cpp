#pragma once

#include <iostream>
#include "channel.h"
#include "sample_log.h"


using namespace StackFlows;

llm_channel_obj::llm_channel_obj(const std::string &_publisher_url,
                                  const std::string &inference_url,
                                  const std::string &unit_name)
    : unit_name_(unit_name), inference_url_(inference_url), publisher_url_(_publisher_url)
{
    zmq_url_index_ = -1000;
    zmq_[-1] = std::make_shared<pzmq>(publisher_url_, ZMQ_PUB);
    zmq_[-2].reset();
}

llm_channel_obj::~llm_channel_obj()
{
    std::cout << "llm_channel_obj 析构" << std::endl;
}

/**
 * 主要功能
 * 处理从ZMQ订阅者接收到的消息，提取关键信息并转发给业务回调函数
 */
void llm_channel_obj::subscriber_event_call(const std::function<void(const std::string &, const std::string &)> &call,
                                            pzmq *_pzmq,
                                            const std::shared_ptr<pzmq_data> &raw)
{
    // 消息解析 - 将原始ZMQ数据转换为字符串
    auto _raw = raw->string();

    // 查找action标识 - 寻找包含"action"字段的JSON消息
    const char *user_inference_flage_str = "\"action\"";
    std::size_t pos = _raw.find(user_inference_flage_str);
    while (true)
    {
        if (pos == std::string::npos)
        {
            break;
        }
        else if ((pos > 0) && (_raw[pos - 1] != '\\'))
        {

            // 提取消息元数据
            std::string zmq_com = sample_json_str_get(_raw, "zmq_com");

            // 设置响应通道：
            if (!zmq_com.empty())
            {
                set_push_url(zmq_com);
            }
            request_id_ = sample_json_str_get(_raw, "request_id");
            work_id_ = sample_json_str_get(_raw, "work_id");
            break;
        }
        pos = _raw.find(user_inference_flage_str, pos + sizeof(user_inference_flage_str));
    }

    // 转发业务数据 - 调用用户提供的回调函数
    call(sample_json_str_get(_raw, "object"), sample_json_str_get(_raw, "data"));
}

/**
 * 主要功能
 * 建立ZMQ订阅连接，用于接收特定work_id的推理输入数据
 * 核心作用
 * 建立数据输入通道，让LLM任务能够接收来自外部的推理请求数据, 实现异步消息订阅机制。
 */
int llm_channel_obj::subscriber_work_id(const std::string &work_id,
                                        const std::function<void(const std::string &, const std::string &)> &call)
{
    int id_num;
    std::string subscriber_url;
    std::regex pattern(R"((\w+)\.(\d+))");
    std::smatch matches;

    // 确定订阅URL：
    // - 如果work_id有效：查询系统获取专用输出端口URL
    if ((!work_id.empty()) && std::regex_match(work_id, matches, pattern))
    {
        if (matches.size() == 3)
        {
            id_num = std::stoi(matches[2].str());
            std::string input_url_name = work_id + ".out_port";
            std::string input_url = unit_call("sys", "sql_select", input_url_name);
            if (input_url.empty())
            {
                return -1;
            }
            subscriber_url = input_url;
        }
    }

    // - 如果work_id为空：使用默认推理URL (inference_url_)
    else
    {
        id_num = 0;
        subscriber_url = inference_url_;
    }

    // 创建ZMQ订阅者：
    zmq_[id_num] = std::make_shared<pzmq>(
        subscriber_url, ZMQ_SUB,
        std::bind(&llm_channel_obj::subscriber_event_call,
                  this,
                  call,
                  std::placeholders::_1,
                  std::placeholders::_2));
    return 0;
}

void llm_channel_obj::stop_subscriber(const std::string &zmq_url)
{
    if (zmq_url.empty())
    {
        zmq_.clear();
        zmq_url_map_.clear();
    }
    else if (zmq_url_map_.find(zmq_url) != zmq_url_map_.end())
    {
        zmq_.erase(zmq_url_map_[zmq_url]);
        zmq_url_map_.erase(zmq_url);
    }
}

int llm_channel_obj::send_raw_to_pub(const std::string &raw)
{
    return zmq_[-1]->send_data(raw);
}

/**
 * send_raw_to_usr: 频繁向同一用户发送数据
 */
int llm_channel_obj::send_raw_to_usr(const std::string &raw)
{
    if (zmq_[-2])
    {
        return zmq_[-2]->send_data(raw);
    }
    else
    {
        return -1;
    }
}

void llm_channel_obj::set_push_url(const std::string &url)
{
    if (output_url_ != url)
    {
        output_url_ = url;
        zmq_[-2].reset(new pzmq(output_url, ZMQ_PUSH));
    }
}

void llm_channel_obj::cear_push_url()
{
    zmq_[-2].reset();
}

/**
 * send_raw_for_url: 偶尔向不同地址发送数据
 */
int llm_channel_obj::send_raw_for_url(const std::string &zmq_url,
                                       const std::string &raw)
{
    pzmq _zmq(zmq_url, ZMQ_PUSH);
    return _zmq.send_data(raw);
}