#pragma once

#include <memory>
#include <string>

#include "zmq.h"

namespace StackFlows {

class pzmq_data {

public:
    pzmq_data();
    ~pzmq_data();

    // Message access methods
    std::shared_ptr<std::string> get_string();
    std::string string();
    void *data();
    size_t size();
    zmq_msg_t *get();

    // Parameter handling methods
    std::string get_param(int index, const std::string& idata = "");
    static std::string set_param(std::string parma0, std::string param1);

private:
    zmq_msg_t msg;

};

} // namespace StackFlows