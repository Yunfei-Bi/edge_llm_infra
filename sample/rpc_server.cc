#include <iostream>
#include <string>

#include "pzmq.hpp"
#include "pzmq_data.h"

using namespace StackFlows;

std::string fun1_(pzmq *self, const std::shared_ptr<pzmq_data>& msg) {
    std::string raw_msg = msg->string();
    std::cout << "fun1 received: " << raw_msg << std::endl;

    return "hello fun1";
}

std::string fun2_(pzmq *self, const std::shared_ptr<pzmq_data>& msg) {
    std::string raw_msg = msg->string();
    std::cout << "fun2 recevied: " << raw_msg << std::endl;

    return "hello fun2";
}

int main(int argc, char *argv[]) {

    /**
     * 这里正是在注册 RPC 服务：
     * pzmq _rpc("test") - 创建名为 "test" 的 RPC 服务器
     * _rpc.register_rpc_action("fun1", fun1_) - 注册名为 "fun1" 的 RPC 函数，绑定到  fun1_ 实现
     * _rpc.register_rpc_action("fun2", fun2_) - 注册名为 "fun2" 的 RPC 函数，绑定到 fun2_ 实现
     */

    /**
     * 这表明 pzmq 不仅仅是 ZeroMQ 的简单封装，
     * 而是在其基础上实现了更高级的 RPC 功能。
     * 它可能在内部使用 ZeroMQ 的 REQ-REP 模式，
     * 但对外提供了更便于使用的 RPC 接口，包括：
     * 
     * 函数注册机制
     * 消息序列化/反序列化
     * 请求路由
     * 异步调用支持
     */
    pzmq _rpc("test");
    _rpc.register_rpc_action("fun1", fun1_);
    _rpc.register_rpc_action("fun2", fun2_);

    while (1) {
        sleep(1);
    }

    return 0;
}