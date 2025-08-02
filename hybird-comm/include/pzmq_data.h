#pragma once
#include "zmq.h"
#include <memory>
#include <string>

namespace StackFlows
{
class pzmq_data
{
private:
    zmq_msg_t msg;
public:
    pzmq_data();
    ~pzmq_data();

    // 消息方法
    std::shared_ptr<std::string> get_string();
    std::string string();
    void *data();
    size_t size();
    zmq_msg_t *get();

    // 参数处理方法
    std::string get_param(int index, const std::string &idata = "");
    static std::string set_param(std::string param0, std::string param1);
};
} // namespace StackFlows