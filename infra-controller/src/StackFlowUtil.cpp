#include <vector>
#include <glob.h>
#include <fstream>

#include "StackFlowUtil.h"
#include "pzmq.hpp"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

/**
 * 这个函数的作用是从JSON字符串中提取指定键的值
 * 使用实例：
 * // JSON: {"action": "inference", "data": {"key": "value"}}
 * std::string action = sample_json_str_get(json_str, "action");  // 返回: inference
 * std::string data = sample_json_str_get(json_str, "data");      // 返回: {"key": "value"}
 */
std::string StackFlows::sample_json_str_get(const std::string &json_str, const std::string &json_key) {
    std::string key_val;
    std::string format_val;
    std::string find_key = "\"" + json_key + "\"";
    
    int subs_start = json_str.find(find_key);
    if (subs_start == std::string::npos) {
        return key_val;
    }

    int status = 0;
    char last_c = '\0';
    int obj_flage = 0;
    for (auto c : json_str.substr(subs_start + find_key.length())) {
        switch (status) {
            case 0: {
                switch (c) {
                    case '"': {
                        status = 100;
                    } break;
                    case '{': {
                        key_val.push_back(c);
                        obj_flage = 1;
                        status    = 10;
                    } break;
                    case ':':
                        obj_flage = 1;
                        break;
                    case ',':
                    case '}': {
                        obj_flage = 0;
                        status    = -1;
                    } break;
                    case ' ':
                        break;
                    default: {
                        if (obj_flage) {
                            key_val.push_back(c);
                        }
                    } break;
                }
            } break;
            case 10: {
                key_val.push_back(c);
                if (c == '{') {
                    obj_flage++;
                }
                if (c == '}') {
                    obj_flage--;
                }
                if (obj_flage == 0) {
                    if (!key_val.empty()) {
                        status = -1;
                    }
                }
            } break;
            case 100: {
                if ((c == '"') && (last_c != '\\')) {
                    obj_flage = 0;
                    status    = -1;
                } else {
                    key_val.push_back(c);
                }
            } break;
            default:
                break;
        }
        last_c = c;
    }
    if (obj_flage != 0) {
        key_val.clear();
    }

    return key_val;
}

/**
 * 这个函数的作用是从work_id字符串中提取数字部分。
 * sample_get_work_id_num("task.123");    // 返回: 123
 * sample_get_work_id_num("worker.456");  // 返回: 456
 * sample_get_work_id_num("invalid");     // 返回: WORK_ID_NONE (-100)
 * sample_get_work_id_num("task.");       // 返回: WORK_ID_NONE (-100)
 */
int StackFlows::sample_get_work_id_num(const std::string &work_id) {
    int a = work_id.find(".");
    if ((a == std::string::npos) || (a == work_id.length() - 1)) {
        return WORK_ID_NONE;
    }
    return std::stoi(work_id.substr(a + 1));
}

std::string StackFlows::sample_get_work_id_name(const std::string &work_id) {
    int a = work_id.find(".");
    if (a == std::string::npos) {
        return work_id;
    } else {
        return work_id.substr(0, a);
    }
}

std::string StackFlows::sample_get_work_id(int work_id_num, const std::string &unit_name) {
    return unit_name + "." + std::to_string(work_id_num);
}

/**
 * 这个函数的作用是将Unicode码点转换为UTF-8编码
 * 
 * 1. 单字节字符 (0x00-0x7F) 例如：'A' (U+0041) → 0x41
 * 2. 双字节字符 (0x80-0x7FF) 例如：'é' (U+00E9) → 0xC3 0xA9
 * 3. 三字节字符 (0x800-0xFFFF) 例如：'中' (U+4E2D) → 0xE4 0xB8 0xAD
 * 4. 四字节字符 (0x10000-0x10FFFF) 例如：emoji字符
 */
void StackFlows::unicode_to_utf8(unsigned int codepoint, char *output, int *length) {
    if (codepoint <= 0x7F) {
        output[0] = codepoint & 0x7F;
        *length = 1;
    } else if (codepoint <= 0x7FF) {
        output[0] = 0xC0 | ((codepoint >> 6) & 0x1F);
        output[1] = 0x80 | (codepoint & 0x3F);
        *length = 2;
    } else if (codepoint <= 0xFFFF) {
        output[0] = 0xE0 | ((codepoint >> 12) & 0x0F);
        output[1] = 0x80 | ((codepoint >> 6) & 0x3F);
        output[2] = 0x80 | (codepoint & 0x3F);
        *length = 3;
    } else if (codepoint <= 0x10FFFF) {
        output[0] = 0xF0 | ((codepoint >> 18) & 0x07);
        output[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        output[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        output[3] = 0x80 | (codepoint & 0x3F);
        *length = 4;
    } else {
        *length = 0;
    }
}

/**
 * 这个函数的作用是处理流式数据的解码和重组。
 * 
 * 典型的流式数据格式：
 * {"index": 0, "delta": "Hello", "finish": "true"}
 * {"index": 1, "delta": " ", "finish": "true"} 
 * {"index": 2, "delta": "World", "finish": "false"}  // 最后一片段
 */
bool StackFlows::decode_stream(const std::string &in, std::string &out, 
                                std::unordered_map<int, std::string> &stream_buff) {
    /**
     * index：数据片段的序号
     * finish：是否为最后一个片段的标志
     * delta：当前片段的实际数据内容
     * 将数据片段存储到缓冲区的对应位置
     */
    int index  =std::stoi(StackFlows::sample_json_str_get(in, "index")); 
    std::string finish = StackFlows::sample_json_str_get(in, "finish");
    stream_buff[index] = StackFlows::sample_json_str_get(in, "delta");

    // 流式传输结束，重组数据
    if (finish.find("f") == std::string::npos) {
        for (size_t i = 0; i < stream_buff.size(); ++i) {
            out += stream_buff.at(i);
        }
        stream_buff.clear();
        return false; // 表示流式传输完成
    }
    return true; // 表示还有更多数据片段
}

/**
 *  unit_call 用于调用远程服务的指定方法并返回结果
 */
std::string StackFlows::unit_call(const std::string &unit_name, const std::string &unit_action, 
                 const std::string &data) {
    std::string value;
    pzmq _call(unit_name);
    _call.call_rpc_action(unit_action, data, [&value](
        pzmq *_pzmq, 
        const std::shared_ptr<pzmq_data> &raw) {
            value = raw->string(); //  unit_call 用于调用远程服务的指定方法并返回结果
        });
    return value; //返回结果
}   

void StackFlows::unit_call(const std::string &unit_name, const std::string &unit_action, 
                const std::string &data, 
                std::function<void(const std::shared_ptr<StackFlows::pzmq_data> &)> callback) {
    std::string value;
    StackFlows::pzmq _call(unit_name);
    _call.call_rpc_action(unit_action, data, [callback](
        StackFlows::pzmq *_pzmq, 
        const std::shared_ptr<StackFlows::pzmq_data> &raw) {
            callback(raw);
        });
}

/**
 *  file_exists 用于判断指定路径的文件是否存在且可访问
 * 
 * good() 方法检查流的状态
 * 返回 true：文件存在且可正常读取
 * 返回 false：文件不存在、无权限或其他错误
 */
bool StackFlows::file_exists(const std::string &filePath) {
    std::ifstream file(filePath);
    return file.good();
}