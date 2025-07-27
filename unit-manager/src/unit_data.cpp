#include "unit_data.h"

/**
 * 这是一个典型的资源管理类，封装了ZMQ通信细节，
 * 提供简单的初始化、发送消息和资源清理接口。
 * 主要用于单元间的消息发布通信。
 */
unit_data::unit_data() {

}

void unit_data::init_zmq(const std::string &url) {
    inference_url = url;
    user_inference_chennal_ = std::make_unique<pzmq>(inference_url, ZMQ_PUB);
}

void unit_data::send_msg(const std::string &json_str) {
    user_inference_chennal_->send_data(json_str);
}

unit_data::~unit_data() {
    user_inference_chennal_.reset();
}