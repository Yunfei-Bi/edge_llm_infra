#pragma once

// #define __cplusplus 1

#include <semaphore.h>
#include <unistd.h>
#include <iostream>

#include <string>
#include <list>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <eventpp/eventqueue.h>
#include <thread>
#include <memory>
#include "json.hpp"
#include <regex>
#include "pzmq.hpp"
#include "StackFlowUtil.h"
#include "channel.h"

namespace StackFlows {
class StackFlow {
public:
    typedef enum {
        EVENT_NONE = 0,
        EVENT_SETUP,
        EVENT_EXIT,
        EVENT_PAUSE,
        EVENT_TASKINFO,
    } LOCAL_EVENT;

    std::string unit_name_;
    std::string request_id_;
    std::string out_zmq_url_;

    std::atomic<bool> exit_flage_;
    std::atomic<int> status_;

    // 线程安全的事件队列，存储事件类型
    eventpp::EventQueue<int, void(const std::shared_ptr<void> &)> event_queue_;
    std::unique_ptr<std::thread> even_loop_thread_;

    std::unique_ptr<pzmq> rpc_ctx_;
    std::unordered_map<int, std::shared_ptr<llm_channel_obj>> llm_task_channel_;

    template <typename T>
    std::shared_ptr<llm_channel_obj> get_channel(T workid) {
        int _work_id_num;
        if constexpr (std::is_same<T, int>::value) {
            _work_id_num = workid;
        } else if constexpr (std::is_same<T, std::string>::value) {
            _work_id_num = sample_get_work_id_num(workid);
        } else {
            return nullptr;
        }
        return llm_task_channel_.at(_work_id_num);
    }
    /**
     * - 层级：更高层的抽象，属于StackFlow基类
     * - 用途：用于系统级响应，向远程调用方发送结果
     * - 参数：需要显式指定work_id和可选的zmq_url
     * - 行为：直接创建新的ZMQ连接发送数据
     */
    int send(const std::string &object, 
            const nlohmann::json &data,
            const std::string &error_msg,
            const std::string &work_id,
            const std::string &zmq_url = "")
    {
        nlohmann::json out_body;
        out_body["request_id"] = request_id_;
        out_body["work_id"] = work_id;
        out_body["created"] = time(NULL);
        out_body["object"] = object;
        out_body["data"] = data;
        if (error_sg.empty()) {
            out_body["error"]["code"] = 0;
            out_body["error"]["message"] = "";
        } else {
            out_body["error"] = error_msg;
        }

        if (zmq_url.empty()) {
            pzmq _zmq(out_zmq_url, ZMQ_PUSH);
            std::string out = out_body.dump();
            out += "\n";
            return _zmq.send_data(out);
        } else {
            pzmq _zmq(zmq_url, ZMQ_PUSH);
            std::string out = out_body.dump();
            out += "\n";
            return _zmq.send_data(out);
        }
    }
};
} // namespace StackFlows