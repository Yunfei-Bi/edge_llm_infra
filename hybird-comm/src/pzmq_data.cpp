#include "pzmq_data.h"

namespace StackFlows {

pzmq_data::pzmq_data() {
    zmq_msg_init(&msg);
}

pzmq_data::~pzmq_data() {
    zmq_msg_close(&msg);
}

std::shared_ptr<std::string> pzmq_data::get_string()
{
    auto len = zmq_msg_size(&msg);
    return std::make_shared<std::string>((const char *)zmq_msg_data(&msg), len);
}

std::string pzmq_data::string()
{
    auto len = zmq_msg_size(&msg);
    return std::string((const char *)zmq_msg_data(&msg), len);
}

void* pzmq_data::data()
{
    return zmq_msg_data(&msg);
}

size_t pzmq_data::size() 
{
    return zmq_msg_size(&msg);
}

zmq_msg_t* pzmq_data::get()
{
    return &msg;
}

/**
 * 这个 get_param 函数是用来从消息数据中提取参数的，
 * 它实现了一个简单的参数编码/解码协议：
 * 
 * 数据格式：
 * 消息数据按以下格式存储：
 * [param0_length][param0_data][param1_data]
 * 第一个字节存储 param0 的长度
 * 接下来是 param0 的实际数据
 * 剩余部分是 param1 的数据
 * 
 * 参数提取逻辑：
 * index = 0（偶数）：提取第一个参数
 * 从位置 1 开始，长度为 data[0]
 * index = 1（奇数）：提取第二个参数
 * 从位置 data[0] + 1 开始，长度为剩余所有数据
 * 
 * 使用场景：
 * 在 RPC 示例中可以看到：
 * std::string param0 = msg->get_param(0);  // 获取第一个参数
 * std::string param1 = msg->get_param(1);  // 获取第二个参数
 * 这样就能从一条消息中解析出多个参数，
 * 配合 set_param 静态方法实现参数的打包和解包。
 */
std::string pzmq_data::get_param(int index, const std::string& idata) {
    const char *data = nullptr;
    int size = 0;

    /**
     * 这段代码是在决定数据来源：
     * 两种数据源选择：
     * 使用外部数据（idata.length() > 0）：
     * 使用内部消息数据（else）
     * 
     *  data 是指向传入数据源的起始位置。
     * idata.c_str() 返回一个指向  idata 字符串内部数据的指针（C风格字符数组的起始位置）。
     * 这样  data 指针就指向了外部传入的字符串数据的第一个字符，
     * 后续的参数提取操作将基于这个起始位置进行偏移计算
     */
    if (idata.length() > 0) {
        data = idata.c_str(); // 使用传入的  idata 参数作为数据源
        size = static_cast<int>(idata.length()); // 获取外部数据的长度
    } else {
        data = static_cast<const char *>(zmq_msg_data(&msg)); // 使用对象内部的 ZMQ 消息数据
        size = static_cast<int>(zmq_msg_size(&msg));// 获取内部消息的长度
    }

    /**
     * 这段代码是根据  index 参数来提取不同位置的参数：
     * 
     * 当  index 为偶数（0, 2, 4...）时
     * data + 1：从第2个字节开始（跳过长度字节）
     * data[0]：第1个字节存储的长度值
     * 提取第一个参数
     * 
     * 当  index 为奇数（1, 3, 5...）时
     * data + data[0] + 1：跳过长度字节 + 第一个参数的所有数据
     * size - data[0] - 1：总长度 - 第一个参数长度 - 长度字节 = 第二个参数长度
     * 提取第二个参数
     * 
     * 数据布局示例：
     * [5][h][e][l][l][o][w][o][r][l][d]
     *  ↑   ←─── param0 ───→ ←─ param1 ─→
     * 长度
     * get_param(0) 返回 "hello"
     * get_param(1) 返回 "world"
     */
    if ((index % 2) == 0) {
        return std::string(data + 1, static_cast<size_t>(data[0]));
    } else {
        return std::string(data + data[0] + 1, static_cast<size_t>(size - data[0] - 1));
    }
}

std::string pzmq_data::set_param(std::string param0, std::string param1) {
    std::string data = " " + param0 + param1;
    data[0] = static_cast<char>(param0.length());

    return data;
}

} // namespace StackFlows