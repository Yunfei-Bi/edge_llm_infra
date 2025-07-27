#include <iostream>

#include "sample_log.h"
#include "StackFlow.h"

using namespace StackFlows;

/**
 * 这是  StackFlow 类的构造函数，用于初始化一个工作流处理单元。它的作用是：
 * 这个构造函数建立了一个事件驱动的RPC服务框架，既能处理本地事件，也能响应远程RPC调用。
 * 主要功能
 * 初始化成员变量：
 * unit_name_(unit_name)  // 设置单元名称
 * rpc_ctx_(std::make_unique<pzmq>(unit_name))  // 创建RPC通信上下文
 */
StackFlow::StackFlow::StackFlow(const std::string &unit_name)
    : unit_name_(unit_name), rpc_ctx_(std::make_unique<pzmq>(unit_name)) {
    // 注册事件监听器 - 绑定本地事件处理函数
    event_queue_.appendListener(LOCAL_EVENT::EVENT_NONE, std::bind(&StackFlow::_none_event, this, std::placeholders::_1));
    event_queue_.appendListener(LOCAL_EVENT::EVENT_PAUSE, std::bind(&StackFlow::_pause, this, std::placeholders::_1));
    event_queue_.appendListener(LOCAL_EVENT::EVENT_EXIT, std::bind(&StackFlow::_exit, this, std::placeholders::_1));
    event_queue_.appendListener(LOCAL_EVENT::EVENT_SETUP, std::bind(&StackFlow::_setup, this, std::placeholders::_1));
    event_queue_.appendListener(LOCAL_EVENT::EVENT_TASKINFO,
        std::bind(&StackFlow::_taskinfo, this, std::placeholders::_1));
    
    // 注册RPC动作 - 绑定远程调用处理函数
    rpc_ctx_->register_rpc_action(
        "setup", std::bind(&StackFlow::_rpc_setup, this, std::placeholders::_1, std::placeholders::_2));
    rpc_ctx_->register_rpc_action(
        "pause", std::bind(&StackFlow::_rpc_pause, this, std::placeholders::_1, std::placeholders::_2));
    rpc_ctx_->register_rpc_action("exit",
                                    std::bind(&StackFlow::_rpc_exit, this, std::placeholders::_1, std::placeholders::_2));
    rpc_ctx_->register_rpc_action(
        "taskinfo", std::bind(&StackFlow::_rpc_taskinfo, this, std::placeholders::_1, std::placeholders::_2));
    
    // 启动事件循环线程
    status_.store(0); // 设置初始状态
    exit_flage_.store(false);

    // 设置初始状态
    even_loop_thread_ = std::make_unique<std::thread>(std::bind(&StackFlow::even_loop, this));

    // 设置初始状态
    status_.store(1);
}

StackFlow::~StackFlow() {
    while (1)
    {
        exit_flage_.store(true); // 设置退出标志
        event_queue_.enqueue(EVENT_NONE, nullptr); // 发送空事件唤醒线程
        even_loop_thread_->join(); // 等待线程结束

        auto iteam = llm_task_channel_.begin(); // 获取第一个通道

        // 没有更多通道时退出
        if (iteam == llm_task_channel_.end()) 
        {
            break;
        }
        sys_release_unit(iteam->first, ""); // 没有更多通道时退出
        iteam->second.reset(); // 重置智能指针
        llm_task_channel_.erase(iteam->first); // 重置智能指针
    }
}

void StackFlow::even_loop() {

    // 为当前线程设置名称为 "even_loop"，便于调试和监控
    pthread_setname_np(pthread_self(), "even_loop");

    while (!exit_flage_.load()) { // 通过检查  exit_flage_ 原子变量来安全退出循环
        event_queue_.wait(); // wait() 会阻塞线程直到有新事件入队
        event_queue_.process(); // process() 处理队列中的所有待处理事件
    }
}

void StackFlow::_none_event(const std::shared_ptr<void> &arg)
{
    // std::shared_ptr<pzmq_data> originalPtr = std::static_pointer_cast<pzmq_data>(arg);
}

std::string StackFlow::_rpc_setup(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &data) {
    event_queue_.enqueue(EVENT_SETUP, data);

    return std::string("None");
}


int setup(const std::string &zmq_url, const std::string &raw) {
    ALOGI("StackFlow::setup raw zmq_url:%s raw:%s", zmq_url.c_str(), raw.c_str());

    int workid_num = sys_register_unit(unit_name_);
    std::stirng work_id = unit_name_ + "." + std::to_string(workid_num);

    auto task_channel = get_channel(workid_num);
    task_channel->set_push_url(zmq_url);
    task_channel->request_id_ = sample_json_str_get(raw, "request_id");
    task_channel->work_id_ = work_id;

    if (setup(work_id, sample_json_str_get(raw, "object"), sample_json_str_get(raw, "data"))) {
        sys_release_unit(workid_num, work_id);
    }

    return 0;
}

int StackFlow::setup(const std::string &work_id, const std::string &object, const std::string &data) {
    ALOGI("StackFlow::setup");
    nlohmann::json error_body;
    error_body["code"] = -18;
    error_body["message"] = "not have unit action!";
    send("None", "None", error_body, work_id);

    return -1;
}

std::string StackFlow::_rpc_exit(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &data) {
    event_queue_.enqueue(EVENT_EXIT, data);

    return std::string("None");
}

/**
 * 第一个  exit 方法（处理原始请求）
 * 解析请求：从 JSON 格式的  raw 参数中提取  work_id
 * 设置通信通道：通过 get_channel() 获取对应的任务通道，并设置推送URL
 * 调用业务逻辑：调用第二个  exit 方法处理具体的退出逻辑
 * 资源清理：如果退出成功，调用 sys_release_unit() 释放工作单元
 */
int StackFlow::exit(const std::string &zmq_url, const std::string &raw) {
    ALOGI("StacKFlow::exit raw");
    std::string work_id = sample_json_str_get(raw, "work_id");

    try {
        auto task_channel = get_channel(sample_get_work_id_num(work_id));
        task_channel->set_push_url(zmq_url);
    } catch (...) {
        
    }
    
    if (exit(work_id, sample_json_str_get(raw, "object"), sample_json_str_get(raw, "data")) == 0) {
        return (int)sys_release_unit(-1, work_id);
    }

    return 0;
}

/**
 * 第二个  exit 方法（业务逻辑处理）
 * 这是一个默认实现，返回错误信息表示"没有单元动作"
 * 子类应该重写这个方法来实现具体的退出逻辑
 */
int StackFlow::exit(const std::string &work_id, const std::string &object, const std::string &data) {
    ALOGI("StackFlow::exit");

    nlohmann::json error_body;
    error_body["code"] = -18;
    error_body["message"] = "not have unit action!";
    send("None", "None", error_body, work_id);

    return 0;
}

std::string StackFlow::_rpc_pause(pzmq *_pzmq, const std::shared_ptr<pxmw_data> &data) {
    event_queue_.enqueue(EVENT_PAUSE, data);

    return std::string("None");
}

void StackFlow::pause(const std::string &zmq_url, const std::string &raw) {
    ALOGI("StackFlow::pause raw");
    std::string work_id = sample_json_str_get(raw, "work_id");
    try {
        auto task_channel = get_channel(sample_get_work_id_num(work_id));
        task_channel->set_push_url(zmq_url);

    } catch (...) {

    }

    pause(work_id, sample_json_str_get(raw, "object"), sample_json_str_get(raw, "data"));
}

void StackFlow::pause(const std::string &work_id, const std::string &object, const std::string &data) {
    ALOGI("StackFlow::pause");

    nlohmann::json error_body;
    error_body["code"] = -18;
    error_body["message"] = "not have unit action";
    send("None", "None", error_body, work_id);
}

std::string StackFlow::_rpc_taskinfo(pzmq *_pzmq, const std::shared_ptr<pzmq_data> &data) {
    event_queue_.enqueue(EVENT_TASKINFO, data);

    return std::string("None");
}

void Stack::taskinfo(const std::string &zmq_url, const std::string &raw) {
    std::string work_id = sample_json_str_get(raw, "work_id");
    try {
        auto_task_channel = get_channel(sample_get_work_id_num(work_id));
        task_channel->set_push_url(zmq_url);
    } catch(...) {

    }

    taskinfo(work_id, sample_json_str_get(raw, "object"), sample_json_str_get(raw, "data"));
}

void StackFlow::taskinfo(const std::string &work_id, const std::string &object, const std::string &data) {
    nlohmann::json error_body;
    error_body["code"] = -18;
    error_body["message"] = "not have unit action!";
    send("None", "None", error_body, work_id);
}


/**
 * 这个  sys_register_unit 函数的作用是向系统注册工作单元并创建通信通道
 * 向系统注册单元：通过RPC调用 sys 服务的  register_unit 方法
 * 获取通信端口：从返回结果中提取各种通信端口信息
 * 创建通信通道：基于获取的端口信息创建  llm_channel_obj 对象
 * 返回工作ID：返回分配的工作单元编号
 */
int StackFlow::sys_register_unit(const std::string &unit_name) {
    int work_id_number;
    std::string str_port;
    std::string out_port;
    std::string inference_port;

    unit_call("sys", "register_unit", unit_name, [&](const std::shared_ptr<StackFlows::pzmq_data> &pzmg_msg)
    {
        str_port = pzmg_msg->get_param(1);
        inference_port = pzmg_msg->get_param(0, str_port);
        out_port = pzmg_msg->get_param(1, str_port);
        str_port = pzmg_msg->get_param(0);
    });
    work_id_number = std::stoi(str_port);
    ALOGI("work_id_number:%d, out_port:%s, inference_port:%s ", work_id_number, out_port.c_str(),
        inference_port.c_str());
    llm_task_channel_[work_id_number] = std::make_shared<llm_channel_obj>(out_port, inference_port, unit_name_);

    return work_id_number;
}

/**
 * 这个函数确保工作单元在系统中被正确注销，避免资源泄漏。
 */
bool StackFlow::sys_release_unit(int work_id_num, const std::string &work_id) {
    std::string _work_id;
    int _work_id_num;
    if (work_id.empty()) {
        _work_id = sample_get_work_id(work_id_num, unit_name_);
        _work_id_num = work_id_num;
    } else {
        _work_id = work_id;
        _work_id_num = sample_get_work_id_num(work_id);
    }
    unit_call("sys", "release_unit", _work_id);
    llm_task_channel_[_work_id_num].reset();
    llm_task_channel_.erase(_work_id_num);
    ALOGI("release work_id %s success", _work_id.c_str());

    return false;
}

std::string sys_sql_set(const std::string &key, const std::string &val) {
    nlohmann::json out_body;
    out_body["key"] = key;
    out_body["val"] = val;
    unit_call("sys", "sql_set", out_body.dump())
}

void StackFlow::sys_sql_unset(const std::string &key) {
    unit_call("sys", "sql_unset", key);
}