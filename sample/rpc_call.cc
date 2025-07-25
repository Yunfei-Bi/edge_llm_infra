#include <iostream>
#include <string>

#include "pzmq.hpp"
#include "pzmq_data.h"

using namespace StackFlows;

int main (int argc, char *argv[]) {

    /**
     * pzmq _rpc("test") - 创建 RPC 客户端
     * call_rpc_action() - 异步调用远程函数的方法，包含
     * 函数名（如 "fun1"）
     * 发送的参数（如 "call fun1_"）
     * 回调函数（lambda 表达式）处理响应
     * 
     * 这是一个典型的异步 RPC 调用模式，
     * 客户端发送请求后不阻塞，通过回调函数处理服务端的响应。
     */
    pzmq _rpc("test");
    _rpc.call_rpc_action("fun1", "call fun1_", [](pzmq *self, const std::shared_ptr<pzmq_data>& msg) {
        std::cout << "Response from fun1: " << msg->string() << std::endl;
    });

    _rpc.call_rpc_action("fun2", "call fun2_", [](pzmq *self, const std::shared_ptr<pzmq_data>& msg) {
        std::cout << "Response from fun2: " << msg->string() << std::endl;
    });

    return 0;
}