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
