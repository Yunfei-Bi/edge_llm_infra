#include <iostream>
#include <string>
#include <unistd.h>

#include "pzmq.hpp"
#include "pzmq_data.h"

using namespace StackFlows;

std::string fun1_(pzmq *self, const std::shared_ptr<pzmq_data>& msg) {
    std::string raw_msg = msg->string();
    std::cout << "Raw data(hex): ";

    /**
     * 输出示例：如果接收到字符串 "hello"，会输出：
     * Raw data(hex): 48 65 6C 6C 6F 
     * 这在调试 RPC 通信时很有用，可以看到客户端实际发送的原始字节数据。
     */
    for (char c : raw_msg) {
        printf("%02X ", static_cast<unsigned char>(c));
    }
    std::cout << std::endl;

    std::string param0 = msg->get_param(0);
    std::string param1 = msg->get_param(1);

    std::cout << "fun1 received: param0 = " << param0 << ", param1 = " << param1 << std::endl;

    return pzmq_data::set_param("hello", "sorbai");
}

int main (int argc, char *argv[]) {
    pzmq _rpc("test");
    _rpc.register_rpc_action("fun1", fun1_);

    while (1) {
        sleep(1);
    }

    return 0;
}