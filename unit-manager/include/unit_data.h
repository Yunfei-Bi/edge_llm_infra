#pragma once

#include <vector>
#include "pzmq.hpp"

using namespace StackFlows;

class unit_data {
private:
    std::unique_ptr<pzmq> user_inference_chennel_;
public:
    std::string work_id;

    /**
     *  - 类型: std::string output_url
        - 作用: 存储单元的ZMQ输出URL地址
        - 用途: 用于unit-manager分配给每个单元的输出端点
        - 示例: "tcp://localhost:5555" 或socket文件路径

        1. unit_data.output_url: unit-manager层面，用于单元资源分配
        2. llm_channel_obj.output_url_: infra-controller层面，用于LLM通道通信
     */
    std::string output_url;
    std::string inference_url;
    int port;

    unit_data();
    void init_zmq(const std::string &url);
};

