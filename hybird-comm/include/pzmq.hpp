#pragma once

#include <libzmq/zmq.h>
#include <memory>
#include <functional>
#include <thread>
#include <iostream>
#include <string>
#include <atomic>
#include <unordered_map>
#include <unistd.h>
#include <mutex>
#include <vector>

#include "pzmq_data.h"

#define ZMQ_RPC_FUN (ZMQ_REP | 0x80)
#define ZMQ_RPC_CALL (ZMQ_REQ | 0x80)

namespace StackFlows {

class pzmq {
public:
    using rpc_callback_fun = std::function<std::string(pzmq *, const std::shared_ptr<pzmq_data> &)>;
    using msg_callback_fun = std::function<void(pzmq *, const std::shared_ptr<pzmq_data> &)>;

public:
    const int rpc_url_head_length = 6;
    std::string rpc_url_head_ = "ipc:///tpm/rpc.";
    void *zmq_ctx_;
    void *zmq_socket_;
    std::unordered_map<std::string, rpc_callback_fun> zmq_fun_;
    std::mutex zmq_fun_mtx_;
    std::atomic<bool> flage_;
    std::unique_ptr<std::thread> zmq_thread_;
    int mode_;
    std::string rpc_server_;
    std::string zmq_url_;
    int timeout_;

    bool is_bind() {
        if ((mode_ == ZMQ_PUB) || (mode_ == ZMQ_PULL) || (mode_ == ZMQ_RPC_FUN)) {
            return true;
        } else {
            return false;
        }
    }

public:

    /**
     * RPC 通信创建-惰性
     * 惰性体现在zmq_ctx_(NULL), zmq_socket_(NULL)
     * 节省资源：如果对象创建后没有使用，不会浪费系统资源
     * 提高启动速度：构造函数执行更快
     * 避免无用开销：只有真正需要时才分配网络资源
     * 
     * 构造时设为空：zmq_ctx_(NULL), zmq_socket_(NULL) 表示构造函数执行时不创建任何 ZMQ 资源
     * 延迟到使用时：真正的 ZMQ 上下文和套接字创建会推迟到调用 register_rpc_action() 或 call_rpc_action() 等方法时
     * 按需分配：只有当程序真正需要进行 RPC 通信时，才会调用 zmq_ctx_new() 和 zmq_socket() 创建实际资源
     * 所以 NULL 值就是惰性初始化的标志，表示"资源尚未创建，等需要时再说"。
     */
    pzmq(const std::string &server) 
        : zmq_ctx_(NULL), zmq_socket_(NULL), rpc_server_(server), flage_(true), timeout_(3000) {
        if (server.find("://") != std::string::npos) {
            rpc_url_head_.clear();
        }
    }

    // 具体通信模式创建
    pzmq(const std::string &url, int mode, const msg_callback_fun &raw_call = nullptr)
        : zmq_ctx_(NULL), zmq_socket_(NULL), mode_(mode), flage_(true), timeout_(3000) {
        if ((url[0] != 'i') && (url[1] != 'p')) {
            rpc_url_head_.clear();
        }
        if (mode_ != ZMQ_RPC_FUN) {
            creat(url, raw_call);
        }
    }

    void set_timeout(int ms) {
        timeout_ = ms;
    }

    int get_timeout() {
        return timeout_;
    }

    /**
     * 这个函数是用来列出当前 RPC 服务器注册的所有可用函数的：
     * 功能：
     * 遍历 zmq_fun_ 容器（存储所有注册的 RPC 函数）
     * 将函数名组装成 JSON 格式返回
     * 
     * 代码逻辑：
     * 预分配内存：action_list.reserve(128) 为字符串预留空间，避免频繁重新分配
     * 
     * JSON 开头：action_list = "{\"actions\":["
     * 
     * 遍历函数:
     * zmq_fun_.begin() 到 zmq_fun_.end() 遍历所有注册的函数
     * i->first 是函数名（如 "fun1", "fun2"）
     * 用双引号包围每个函数名
     * 
     * 分隔符处理：
     * 非最后一个函数：添加逗号 ,
     * 最后一个函数：添加 ]} 结束 JSON
     * 
     * 返回示例：
     * {"actions":["fun1","fun2","fun3"]}
     * 
     * 用途：
     * 客户端可以调用这个函数来查询服务器提供了哪些 RPC 函数，实现服务发现功能。
     */
    std::string _rpc_list_action(pzmq *self, const std::shared_ptr<pzmq_data>& _None) {
        std::string action_list;

        /**
         * 预分配内存空间：为action_list这个vector容器预先分配能容纳128个元素的内存空间
         * 性能优化：避免在后续添加元素时频繁进行内存重新分配和数据拷贝操作
         * 不改变size：reserve()只是预分配内存，不会改变vector的实际大小（size），vector仍然是空的
         * 提高效率：如果你知道大概需要存储多少个元素，提前reserve可以显著提高性能，特别是在循环中频繁添加元素的场景
         */
        action_list.reserve(128);
        action_list = "{\"actions\":[";
        for (auto i = zmq_fun_.begin(); ; ) {
            action_list += "\"";
            action_list += i->first;
            action_list += "\"";
            if (++i == zmq_fun_.end()) {
                action_list += "]}";
                break;
            } else {
                action_list += ",";
            }
        }

        return action_list;
    }

    int register_rpc_action(const std::string& action, const rpc_callback_fun& raw_call) {
        int ret = 0;
        std::unique_lock<std::mutex> lock(zmq_fun_mtx_);
        if (zmq_fun_.find(action) != zmq_fun_.end()) {
            zmq_fun_[action] = raw_call;

            return ret;
        }

        /**
         * 检查是否首次注册：if (zmq_fun_.empty()) - 判断函数映射表是否为空
         * 
         * 构建 RPC 服务 URL：std::string url = rpc_url_head_ + rpc_server_;
         * 
         * 自动注册一个名为 "list_action" 的内置函数：
         * zmq_fun_["list_action"] = std::bind(&pzmq::_rpc_list_action, this, std::placeholders::_1, std::placeholders::_2);
         * 并将其绑定到 _rpc_list_action 方法，用于列出所有可用的 RPC 函数
         * 
         * 创建 ZMQ 资源：ret = creat(url);
         * 首次注册函数时才真正创建 ZMQ 上下文和套接字
         * 这正是前面提到的"惰性初始化"的体现
         */
        if (zmq_fun_.empty()) {
            std::string url = rpc_url_head_ + rpc_server_;
            zmq_fun_["list_action"] = 
                std::bind(&pzmq::_rpc_list_action, this, std::placeholders::_1, std::placeholders::_2);
            ret = creat(url);
        }
        zmq_fun_[action] = raw_call;

        return ret;
    }

    void unregister_rpc_action(const std::string& action) {
        std::unique_lock<std::mutex> lock(zmq_fun_mtx_);
        if (zmq_fun_.find(action) != zmq_fun_.end()) {
            zmq_fun_.erase(action);
        }
    }

    /**
     * 这个  call_rpc_action 函数是 RPC 客户端调用远程函数的核心方法
     * 这是一个同步 RPC 调用：发送请求 → 等待响应 → 处理结果 → 清理资源
     */
    int call_rpc_action(const std::string& action, 
        const std::string& data, const msg_callback_fun& raw_call) {
        int ret;
        std::shared_ptr<pzmq_data> msg_ptr = std::make_shared<pzmq_data>();
        try {

            // 惰性初始化检查：
            if (NULL == zmq_socket) {
                if (rpc_server_.empty()) {
                    return -1;
                }
                std::string url = rpc_url_head_ + rpc_server_;
                mode_ = ZMQ_RPC_CALL;
                ret = creat(url);
                if (ret) {
                    throw ret;
                }
            }

            /**
             * 发送请求
             * 先发送函数名（带  ZMQ_SNDMORE 标志表示还有更多数据）
             * 再发送参数数据
             */
            {
                zmq_send(zmq_socket_, action.c_str(), action.length(), ZMQ_SNDMORE);
                zmq_send(zmq_socket_, data.c_str(), data.length(), 0);
            }

            // 接收响应
            {
                zmq_msg_recv(msg_ptr->get(), zmq_socket_, 0);
            }

            // 处理响应，调用回调函数处理服务器返回的结果
            raw_call(this, msg_ptr);
        } catch (int e) {
            ret = e;
        }

        // 清理资源
        msg_ptr.reset();
        close_zmq();

        return ret;
    }

    /**
     * 这个  creat 函数是创建 ZMQ 套接字和连接的核心方法：
     * 主要功能：
     * 保存 URL：zmq_url_ = url
     * 创建 ZMQ 上下文：循环调用 zmq_ctx_new() 直到成功
     * 创建套接字：根据模式创建对应类型的套接字
     * 根据模式分发：调用不同的具体创建方法
     */
    int creat(const std::string &url, const msg_callback_fun &raw_call = nullptr) {
        zmq_url_= url;
        do {
            zmq_ctx_ = zmq_ctx_new();
        } while (zmq_ctx_ == NULL);
        do { // 提取低6位，去掉自定义标志位
            zmq_socket_ = zmq_socket(zmq_ctx_, mode_ & 0x3f);
        } while (zmq_socket_ == NULL);

        switch (mode_) {
            case ZMQ_PUB: { // 发布者模式
                return creat_pub(url);
            } break;
            case ZMQ_SUB: { //  订阅者模式
                return subscriber_url(url, raw_call);
            } break;
            case ZMQ_PUSH: { // 推送模式
                return creat_push(url);
            } break;
            case ZMQ_PULL: { // 拉取模式
                return creat_pull(url, raw_call);
            } break;
            case ZMQ_RPC_FUN: { // RPC 服务端（REP）
                return creat_rep(url, raw_call);
            } break;
            case ZMQ_RPC_CALL: { // RPC 客户端（REQ）
                return creat_req(url);
            } break;
            default:
                break;
        }

        return 0;
    }

    int send_data(const std::string& raw) {
        return zmq_send(zmq_socket_, raw.c_str(), raw.length(), 0);
    }

    /**
     * 功能：
     * 将 ZMQ 套接字绑定到指定的 URL 地址
     * 用于 PUB-SUB 模式中的发布者端
     * 
     * 参数：
     * url：绑定地址，
     * 如 "ipc:///tmp/5001.socket" 或 "tcp://*:5555"
     * 
     * 返回值：
     * 成功返回 0，失败返回错误码
     * 
     * PUB-SUB 模式特点：
     * 发布者绑定：PUB 套接字使用 zmq_bind() 绑定到地址，等待订阅者连接
     * 订阅者连接：SUB 套接字使用 zmq_connect() 连接到发布者
     * 一对多：一个发布者可以向多个订阅者广播消息
     * 订阅者连接到发布者，支持消息过滤，异步接收消息，具有重连机制
     */
    inline int creat_pub(const std::string &url) {
        return zmq_bind(zmq_socket_, url.c_str());
    }

    /**
     * 这个  creat_push 函数是创建 ZeroMQ PUSH 套接字的方法，用于 PUSH-PULL 模式：
     * PUSH-PULL 模式特点：
     * PUSH 负责发送数据，PULL 负责接收
     * PULL 通常绑定地址，PUSH 连接到该地址
     * 支持负载均衡：多个 PULL 可以分担 PUSH 的消息
     * 具有重连和超时保护机制
     * 异步处理：通过后台线程持续监听和处理消息
     */
    inline int creat_push(const std::string &url) {

        /**
         * 设置重连间隔为 100ms
         * 连接断开后等待 100ms 再尝试重连
         */ 
        int reconnect_interval = 100;
        zmq_setsockopt(zmq_socket_, ZMQ_RECONNECT_IVL, &reconnect_interval, sizeof(reconnect_interval));

        /**
         * 设置最大重连间隔为 1000ms（注释说 5 秒可能有误）
         * 防止重连间隔无限增长
         */
        int max_reconnect_interval = 1000;
        zmq_setsockopt(zmq_socket_, ZMQ_RECONNECT_IVL_MAX, &max_reconnect_interval, sizeof(max_reconnect_interval));

        /**
         * 发送超时
         * 设置发送操作的超时时间
         */
        zmq_setsockopt(zmq_socket_, ZMQ_SNDTIMEO, &timeout_, sizeof(timeout_));

        // 连接到目标，PUSH 套接字主动连接到 PULL 套接字
        return zmq_connect(zmq_socket_, url.c_str());
    }

    /**
     * 这个  creat_pull 函数是创建 ZeroMQ PULL 套接字的方法，用于 PUSH-PULL 模式中的接收端：
     * 1. 绑定套接字：
     * PULL 套接字绑定到指定地址，等待 PUSH 套接字连接
     * 
     * 2. 设置运行标志：
     * flage_ = false 表示套接字已就绪
     * 可能用于控制事件循环的运行状态
     * 
     * 3. 创建后台线程：
     * zmq_thread_ = std::make_unique<std::thread>(std::bind(&pzmq::zmq_event_loop, this, raw_call));
     * 创建一个独立线程运行 zmq_event_loop 方法
     * 传入回调函数 raw_call 用于处理接收到的消息
     * 使用 std::bind 绑定当前对象和回调函数
     */
    inline int creat_pull(const std::string &url, const msg_callback_fun &raw_call) {
        int ret = zmq_bind(zmq_socket_, url.c_str());
        flage_ = false;
        zmq_thread_ = std::make_unique<std::thread>(std::bind(&pzmq::zmq_event_loop, this, raw_call));

        return ret;
    }

    /**
     * 这个  subscriber_url 函数是创建 ZeroMQ 订阅者（Subscriber）套接字的方法，用于 PUB-SUB 模式中的订阅端
     * 
     */

    inline int subscriber_url(const std::string &url, const msg_callback_fun &raw_call) {

        // 设置重连参数，重连间隔 100ms，最大重连间隔 1000ms
        int reconnect_interval = 100;
        zmq_setsockopt(zmq_socket_, ZMQ_RECONNECT_IVL, &reconnect_interval, sizeof(reconnect_interval));

        // 设置最大重连间隔为 1000ms
        int max_reconnect_interval = 1000;
        zmq_setsockopt(zmq_socket_, ZMQ_RECONNECT_IVL_MAX, &max_reconnect_interval, sizeof(max_reconnect_interval));

        // 连接到发布者，SUB 套接字主动连接到 PUB 套接字
        int ret = zmq_connect(zmq_socket_, url.c_str());

        /**
         * 设置订阅过滤：
         * 空字符串 "" 表示订阅所有消息（无过滤）
         * 如果设置特定前缀，只接收匹配的消息
         */
        zmq_setsockopt(zmq_socket_, ZMQ_SUBSCRIBE, "", 0);

        /**
         * 启动后台线程：
         * 创建独立线程运行事件循环
         * 异步接收和处理发布者的消息
         */
        flage_ = false;
        zmq_thread_ = std::make_unique<std::thread>(std::bind(&pzmq::zmq_event_loop, this, raw_call));

        return ret;
    }

    /**
     * 这个  creat_rep 函数是创建 ZeroMQ REP（Reply）套接字的方法，用于 RPC 服务端：
     */
    inline int creat_rep(const std::string &url, const msg_callback_fun &raw_call) {
        int ret = zmq_bind(zmq_socket_, url.c_str());
        flage_ = false;

        /**
         * 创建独立线程运行 zmq_event_loop 方法
         * 异步处理客户端的 RPC 请求
         * 传入回调函数处理具体的业务逻辑
         */
        zmq_thread_ = std::make_unique<std::thread>(std::bind(&pzmq::zmq_event_loop, this, raw_call));

        return ret;
    }

    /**
     * 这个  creat_req 函数是创建 ZeroMQ REQ（Request）套接字的方法，用于 RPC 客户端
     * REQ-REP 模式特点：
     * REQ 客户端连接，REP 服务端绑定
     * 严格的请求-响应模式
     * 用于实现同步 RPC 调用
     */
    inline int creat_req(const std::string &url) {

        /**
         * 如果使用 IPC 协议（ rpc_url_head_ 不为空）
         * 提取套接字文件路径（去掉前缀部分）
         * 使用 access() 检查套接字文件是否存在
         * 如果文件不存在，返回 -1（连接失败）
         */
        if (!rpc_url_head_.empty()) {
            std::string socket_file = url.substr(rpc_url_head_length);
            if (access(socket_file.c_str(), F_OK) != 0) {
                return -1;
            }
        }

        /**
         * 设置超时参数：
         * 设置发送超时时间
         * 设置接收超时时间
         * 防止客户端无限等待
         */
        zmq_setsockopt(zmq_socket_, ZMQ_SNDTIMEO, &timeout_, sizeof(timeout_));
        zmq_setsockopt(zmq_socket_, ZMQ_RCVTIMEO, &timeout_, sizeof(timeout_));

        /**
         * 连接到服务器
         * REQ 套接字主动连接到 REP 服务器
         */
        return zmq_connect(zmq_socket_, url.c_str());
    }

    /**
     * 这个  zmq_event_loop 函数是后台线程的事件循环，用于异步处理不同模式的消息：
     * 1. 设置线程名称为 "zmq_event_loop"，便于调试
     * 2. 创建 zmq_pollitem_t 结构体数组 items，用于轮询
     * 3. 如果是 PULL 模式，将套接字添加到轮询数组
     * 4. 进入主循环，直到 flage_ 标志为 true
     */
    void zmq_event_loop(const msg_callback_fun &raw_call) {
        pthread_setname_np(pthread_self(), "zmq_event_loop");

        int ret;
        zmq_pollitem_t items[1];

        // PULL 模式的轮询设置
        if (mode_ == ZMQ_PULL) {
            items[0].socket = zmq_socket_;
            items[0].fd = 0;
            items[0].events = ZMQ_POLLIN;
            items[0].revents = 0;
        }

        // 循环条件：while (!flage_.load()) - 直到标志位为 true 才退出
        while (!flage_.load()) {
            std::shared_ptr<pzmq_data> msg_ptr = std::make_shared<pzmq_data>();
            if (mode_ == ZMQ_PULL) {
                
                ret = zmq_poll(items, 1, -1); // 无限等待消息
                if (ret == -1) {
                    zmq_close(zmq_socket_);
                    continue;
                }
                if (!items[0].revents & ZMQ_POLLIN) {
                    continue;
                }
            }

            // 接收消息
            ret = zmq_msg_recv(msg_ptr->get(), zmq_socket_, 0);
            if (ret <= 0) {
                msg_ptr.reset();
                continue;
            }

            // RPC 模式特殊处理
            if (mode_ == ZMQ_RPC_FUN) {
                std::shared_ptr<pzmq_data> msg1_ptr = std::make_shared<pzmq_data>();

                // 接收第二部分消息（参数）
                zmq_msg_recv(msg1_ptr->get(), zmq_socket_, 0);
                std::string retval;
                try {
                    std::unique_lock<std::mutex> lock(zmq_fun_mtx_);

                    // 查找并调用对应的 RPC 函数
                    retval = zmq_fun_.at(msg_ptr->string())(this, msg1_ptr);

                } catch (...) {
                    retval = "NotAction";
                }

                // 发送响应
                zmq_send(zmq_socket_, retval.c_str(), retval.length(), 0);
                msg1_ptr.reset();
            } else {
                raw_call(this, msg_ptr);
            }
            msg_ptr.reset();
        }
    }

    void close_zmq() {
        zmq_close(zmq_socket_);
        zmq_ctx_destroy(zmq_ctx_);
        if ((mode_ == ZMQ_PUB) || (mode_ == ZMQ_PULL) || (mode_ == ZMQ_RPC_FUN)) {
            if (!rpc_url_head_.empty()) {
                std::string socket_file = zmq_url_.substr(rpc_url_head_length);
                if (access(socket_file.c_str(), F_OK) == 0) {
                    remove(socket_file.c_str());
                }
            }
        }
        zmq_socket_ = NULL;
        zmq_ctx_ = NULL;
    }

    ~pzmq() {
        if (!zmq_socket_) {
            return ;
        }
        flage_ = true;
        zmq_ctx_shutdown(zmq_ctx_);
        if (zmq_thread_) {
            zmq_thread_->join();
        }
        close_zmq();
    }
};

} // namespace StackFlows