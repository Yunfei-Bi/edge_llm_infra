#include "unit_data.h"

unit_data::unit_data() {}

/**
 * 初始化ZMQ连接，设置推理URL并创建一个ZMQ发布者通道
 */
void unit_data::init_zmq(const std::string &url)
{
    inference_url = url;
    user_inference_chennal_ =
        std::make_unique<pzmq>(inference_url, ZMQ_PUB);
}

/**
 * 通过ZMQ通道发送JSON格式的消息。
 */

void unit_data::send_msg(const std::string &json_str)
{
    user_inference_chennal_->send_data(json_str);
}

/**
 * 析构函数，清理资源，重置ZMQ通道
 */
unit_data::~unit_data()
{
    user_inference_chennal_.reset();
}