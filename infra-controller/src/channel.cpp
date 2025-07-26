#include <iostream>

#include "channel.h"
#include "sample_log.h"

using namespace StackFlows;

llm_channel_obj::llm_channel_obj(const std::string &_publisher_url,
                                const std::string &inference_url, 
                                const std::string &unit_name)
    : unit_name_(unit_name), 
        inference_url_(inference_url),
        publisher_url_(_publisher_url) {
    zmq_url_index_ = -1000; // 初始化索引，暂无作用
    zmq_[-1] = std::make_shared<pzmq>(publisher_url_, ZMQ_PUB); // 初始化发布者，给其他节点模块通信
    zmq_[-2].reset(); // 预留push通道，给外部用户通信
}

llm_channel_obj::~llm_channel_obj() {
    std::cout << "llm_channel_obj 析构" << std::endl;
}

/**
 * subscriber_event_call 解析接收到的JSON消息，提取关键信息并调用用户提供的回调函数。
 * 
 * 查找action字段：在JSON消息中寻找"action"字段
 * 
 * 避免转义字符：确保找到的不是被转义的\"action\"
 * 
 * 提取通信信息：
 * zmq_com - ZMQ通信URL，用于设置push连接
 * request_id - 请求ID
 * work_id - 工作ID
 * 
 * 调用用户回调：传递object和 data字段给用户回调函数
 */
void llm_channel_obj::subscriber_event_call(const std::function<void(const std::string &, 
                                            const std::string &)> &call, 
                                            pzmq *_pzmq, 
                                            const std::shared_ptr<pzmq_data> &raw) {
    
    /**
     * 将ZMQ消息数据转换为字符串格式
     * raw是pzmq_data类型的智能指针，包含接收到的消息
     */
    auto _raw = raw->string();

    /**
     * 定义搜索目标
     * 要在JSON中查找的字段名："action"（包含引号）
     * 这是JSON格式中action字段的标识符
     */
    const char *user_inference_flage_str = "\"action\"";

    /**
     * 开始搜索
     * 在消息字符串中查找"action"的位置
     * 返回第一次出现的位置索引，如果没找到返回std::string::npos
     */
    std::size_t pos = _raw.find(user_inference_flage_str);
    while (true) {
        if (pos == std::string::npos) {
            break;
        } else if ((pos > 0) && (_raw[pos - 1] != '\\')) {
            std::string zmq_com = sample_json_str_get(_raw, "zmq_com");

            /**
             * 检查并设置推送URL
             * !zmq_com.empty() - 检查zmq_com字符串是否不为空
             * 如果不为空，调用set_push_url(zmq_com)设置推送连接的URL
             * 
             * 具体作用
             * zmq_com是从JSON消息中提取的ZMQ通信地址
             * 这个地址用于建立PUSH-PULL模式的连接，向外部用户发送响应
             * 只有当消息中包含有效的zmq_com字段时，才会设置推送URL
             */
            if (!zmq_com.empty()) {
                set_push_url(zmq_com); // 设置外部用户通信的推送地址
            }

            /**
             * 唯一标识：每个RPC请求的唯一标识符，每个RPC请求的唯一标识符
             * work_id_ - 工作标识符，标识具体的工作任务或处理单元，跟踪任务的执行状态
             * 
             * 接收消息：从JSON中提取request_id和work_id
             * 处理请求：使用这些ID进行业务逻辑处理
             * 发送响应：在响应中包含相同的ID，确保客户端能正确匹配请求和响应
             */
            request_id_ = sample_json_str_get(_raw, "request_id");
            work_id_ = sample_json_str_get(_raw, "work_id");
            break;
        }
        pos = _raw.find(user_inference_flage_str, pos + sizeof(user_inference_flage_str));
    }
    call(sample_json_str_get(_raw, "object"), sample_json_str_get(_raw, "data"));
}

void message_handler(pzmq *zmq_obj, const std::shared_ptr<pzmq_data> &data) {
    std::cout << "Received: " << data->string() << std::endl;
}

/**
 * 根据work_id参数决定订阅哪个URL，并创建相应的ZMQ订阅者
 */
int llm_channel_obj::subscriber_work_id(const std::string &work_id,
                                        const std::function<void(const std:;string &, const std::string &)> call) {
    int id_num;
    std::string subscriber_url;

    /**
     * 匹配格式：单词.数字（如task.123）
     * 提取数字部分作为连接ID
     */
    std::regex pattern(R"((\w+)\.(\d+))");
    std::smatch matches;

    /**
     * 两种订阅模式
     * 1. 有效work_id：通过unit_call("sys", "sql_select", ...)查询实际URL
     * 2. 空或无效work_id：使用默认的inference_url_
     */
    if ((!work_id.empty()) && std::regex_match(work_id, matches, pattern)) {
        if (matches.size() == 3) {
            id_num = std::stoi(matches[2].str());

            std::string input_url_name = work_id + "out_port";
            std::string input_url = unit_call("sys", "sql_select", input_url_name);
            if (input_url.empty()) {
                return -1;
            }
            subscriber_url = input_url;
        }
    } else {
        id_num = 0;
        subscriber_url = inference_url_;
    }

    // 创建订阅者
    zmq_[id_num] = std::make_shared<pzmq> (
        subscriber_url, ZMQ_SUB,
        std::bind(&llm_channel_obj::subscriber_event_call, 
            this, 
            call, 
            std::placeholders::_1,
            std::placeholders::_2));

    return 0;
}

void llm_channel_obj::stop_subscriber_work_id(const std::string &work_id) {
    int id_num;
    std::regex pattern(R"((\w+)\.(\d+))");
    std::smatch matches;
    if (std::regex_match(work_id, matches, pattern)) {
        if (matches.size() == 3) {
            id_num = std::stoi(matches[2].str());
        }
    } else {
        id_num = 0;
    }

    if (zmq_.find(id_num) != zmq_end()) {
        zmq_erase(id_num);
    }
}

void llm_channel_obj::subscriber(cont std::string &zmq_url, const pzmq::msg_callback_fun &call) {
    zmq_url_map_[zmq_url] = zmq_url_index_--;
    zmq_[zmq_url_map[zmq_url]] = std::make_shared<pzmq>(zmq_url, ZMQ_SUB, call);
}

void llm_channel_obj::stop_subscriver(const std::string &zmq_url) {
    if (zmq_url.empty()) {
        zmq_.clear();
        zmq_url_map_.clear();
    } else if (zmq_url_map_.find(zmq_url) != zmq_url_map_.end()) {
        zmq_.erase(zmq_url_map_[zmq_url]);
        zmq_url_map_.erase(zmq_url);
    }
}

int llm_channel_obj::send_raw_to_pub(const std::string &raw) {
    return zmq_[-1]->send_data(raw);
}

int llm_channel_obj::send_raw_to_usr(const std::string &raw) {
    if (zmq_[-1]) {
        return zmq_[-2]->send_data(raw);
    } else {
        return -1;
    }
}

void llm_channel_obj::set_push_url(const std::string &url) {
    if (output_url_ != url) {
        output_url_ = url;
        zmq_[-2].reset(new pzmq(output_url_, ZMQ_PUSH));
    }
}

void llm_channel_obj::cear_push_url() {
    zmq_[-2].reset();
}

int llm_channel_obj::send_raw_for_url(const std::string &zmq_url, const std::string &raw) {
    pzmq _zmq(zmq_url, ZMQ_PUSH);
    return _zmq.send_data(raw);
}