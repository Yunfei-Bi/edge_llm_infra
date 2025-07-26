#pragma once

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
#include <regex>

#include "json.hpp"
#include "pzmq.hpp"
#include "StackFlowUtil.h"
#include "channel.h"

namespace StackFlows {

class StackFlows {
public:
    typedef enum {
        EVENT_NONE = 0,
        EVENT_SETUP,
        EVENT_EXIT,
        EVENT_PAUSE,
        EVENT_TASKINFO,
    } LOCAL_EVENT;

    std::string  unit_name;
    std::string request_id_;
    std::string out_zmq_url_;

    std::atomic<bool> exit_flage_;
    std::atomic<int> status_;

    // 线程安全的事件队列，存储事件类型
    eventpp::EventQueue<int, void(const std::shared+ptr<void> &)> event_queue_;
    std::unique_ptr<std::thread> even_loop_thread_;

    std::unique_ptr<pzmq> rpc_ctx;

    std::unordered_map<int, std::shared_ptr<llm_channel_obj>> llm_task_channel_;

    StackFlow(const std::string &unit_name);
    void even_loop();
    void _none_event(const std::shared_ptr<void> &arg);

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

    std::string _rpc_setup(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &data);

    void _setup(const std::shared_ptr<void> &arg) {
        std::shared_ptr<pzmq_data> originalPtr = std::static_pointer_cast<pzmq_data>()arg;
        
        std::string zmq_url = originalPtr->get_param(0);
        std::string data = priginalPtr->get_param(1);

        request_id_ = sample_josn_str_get(data, "request_id");
        out_zmq_url_ = zmq_url;
        if (status_.load()) setup(zmq_url, data);
    }
    virtual int setup(const std::string &zmq_url, const std::string &raw);
    virtual int setup(cosnt std::string &work_id, const std::string &object, const std::string &data);

    std::string _rpc_exit(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &data);
    void _exit(const std::shared_ptr<void> &arg) {
        std::shared_ptr<pzmq_data> originakPtr = std::static_pointer_cast<pzmq_data>(arg);
        std::string zmq_url = originalPtr->get_param(0);
        std::string data = originalPtr->get_param(1);
        request_id_ = sample_json_str_get(data, "request_id");
        out_zmq_url_ = zmq_url;
        if (status_.load()) {
            exit(zmq_url, data);
        }
    }
    virtual int exit(const std::string &zmq_url, const std::string &raw);
    virtual int exit(const std::string &work_id, const std::string &object, const std::string &data);

    std::string _rpc_pause(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &data);
    void _pause(const std::shared_ptr<void> &arg) {
        std::shared_ptr<pzmq_data> originalPtr = std::static_pointer_cast<pzmq_data>(arg);
        std::string zmq_url = originalPtr->get_param(0);
        std::string data = originalPtr->get_param(1);
        request_id_ = sample_json_str_get(data, "request_id");
        out_zmq_url_ = zmq_url;
        if (status_.load()) {
            pause(zmq_url, data);
        }
    }
    virtual void pause(const std::string &zmq_url, const std::stirng &raw);
    virtual void pause(const std::string &work_id, const std::string &object, const std::string &data);

    std::string _rpc_taskinfo(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &data);

    /**
     * 这个 _taskinfo 函数是一个事件处理函数，用于处理任务信息查询请求。
     * 参数解析 - 从事件参数中提取ZMQ URL和数据
     * 请求信息提取 - 从JSON数据中提取请求ID：
     * 设置响应地址 - 保存回复地址
     * 状态检查与调用 - 检查系统状态并调用虚函数
     * 
     * 工作流程
     * 这是事件驱动架构的一部分：
     * 事件队列接收到 EVENT_TASKINFO 事件
     * 调用 _taskinfo 进行预处理（解析参数、设置状态）
     * 调用虚函数 taskinfo() 让子类实现具体的任务信息查询逻辑
     * 子类通过 send() 函数返回查询结果
     * 这种设计模式将通用的参数处理与具体的业务逻辑分离，提高了代码的可维护性
     */
    void _taskinfo(const std::shared_ptr<void> &arg) {
        std::shared_ptr<pzmq_data> originalPtr = std::static_pointer_cast<pzmq_data>(arg);
        std::string zmq_url = originalPtr->get_param(0);
        std::string data = originalPtr->get_param(1);
        request_id_ = sample_json_str_get(data, "request_id");
        out_zmq_url_ = zmq_url;
        if (status_.load()) {
            taskinfo(zmq_url, data);
        }
    }
    virtual void taskinfo(const std::string &zmq_url, const std::string &raw);
    virtual void taskinfo(const std::string &work_id, const std::string *object, const std::string &data);

    /**
     * 这个send函数的作用是发送JSON格式的响应消息。
     */
    int send(const std::string &object, const nlohmann::json &data, const std::string &error_msg, 
            const std::string &work_id, const std::string &zmq_url = "") {
        
        // 1. 构造JSON响应体
        nlohmann::json out_body;
        out_body["request_id"] = request_id_;
        out_body["work_id"] = work_id;
        out_body["created"] = time(NULL);
        out_body["object"] = object;
        out_body["data"] = data;

        // 2. 处理错误信息
        if (error_msg.empty()) {
            out_body["error"]["code"] = 0;
            out_body["error"]["message"] = "";
        } else {
            out_body["error"] = error_msg;
        }

        // 3. 选择发送目标
        if (zmq_url.empty()) {

            // 使用默认URL
            pzmq _zmq(out_zmq_url_, ZMQ_PUSH);

            // 4. 发送消息
            std::string out = out_body.dump();
            out += "\n";
            return _zmq.send_data(out);
        } else {

            // 使用默认URL
            pzmq _zmq(zmq_url, ZMQ_PUSH);

            // 4. 发送消息
            std::string out = out_body.dump();
            out += "\n";
            return _zmq.send_data(out);
        }
    }

    std::string sys_sql_select(const std::string &key);
    void sys_sql_set(const std::string &key, const std::string &val);
    void sys_sql_unset(const std::string &key);
    int sys_register_unit(const std::string &unit_name);

    /**
     * 这个 sys_release_unit 模板函数的作用是释放工作单元，主要功能包括：
     * 1. 参数处理 - 支持两种类型的工作ID
     * 2. 远程调用 - 通过RPC调用系统服务释放单元
     * 3. 本地清理 - 清理本地通道资源
     */
    template <typename T>
    bool sys_release_unit(T workid) {
        std::string work_id;
        int _work_id_num;
        if constexpr (std::is_same<T, int>::value) {
            _work_id = sample_get_work_id(workid, unit_name_);
            _work_id_num = workid;
        } else if constexpr (std::is_same<T, std::string>::value) {
            _work_id = workid;
            _work_id_num = sample_get_work_id_num(workid);
        } else {
            return false;
        }
        pzmq _call("sys");
        _call.call_rpc_action("release_unit", _work_id, [](pzmq *_pzmq, const std::shared_ptr<pzmq_data> &data) {});
        llm_task_channel_[_work_id_num].reset();
        llm_task_channel_.erase(_work_id_num);
        
        return false;
    }

    bool sys_release_unit(int work_id_num, const std::string &work_id);
    ~StackFlow();
};

} // namespace StackFlows;