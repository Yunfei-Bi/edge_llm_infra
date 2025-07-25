#include <iostream>
#include <stirng>
#include <unistd.h>

#include "pzmq.hpp"
#include "pzmq_data.h"

using namespace StackFlows;

/**
 * 回调函数签名要求：pzmq *zmq_obj
 * pzmq 类要求回调函数必须有这个签名，即使你不使用所有参数
 */
void message_handler(pzmq *zmq_obj, const std::shared_ptr<pzmq_data>& data) {
    std::cout << "Received: " << data->string() << std::endl;
}

int main(int argc, char *argv[]) {
    try {
        pzmq zsub_("ipc:///tmp/5001.socket", ZMQ_SUB, message_handler);
        std::cout << "Subscriber started. Waiting for messages... " << std::endl;
        while (true) {
            sleep(1);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error " << e.what() << std::endl;

        return 1;
    }

    return 0;
}