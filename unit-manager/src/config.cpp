#include <fstream>
#include <iostream>

#include "all.h"
#include "json.hpp"

/**
 * 这个函数的作用是从JSON配置文件加载系统配置到全局存储中：
 * 
 * 存储结果
 * 配置会被存储到全局的  key_sql 变量中，
 * 其他代码可以通过  SAFE_READING 宏来读取这些配置。
 */
void load_default_config() {

    // 打开配置文件
    std::ifstream file("../master_config.json");
    if (!file.is_open()) {
        return ;
    }

    // 解析JSON配置
    nlohmann::json req_body;
    try {
        file >> req_body;
    } catch (...) {
        file.close();
        return ;
    }
    file.close();

    // 遍历配置项并存储到全局变量
    for (auto it = req_body.begin(); it != req_body.end(); ++it) {
        if (req_body[it.key()].is_number()) {
            key_sql[(std::string)it.key()] = (int)it.value();
        }
        if (req_body[it.key()].is_string()) {
            key_sql[(std::string)it.key()] = (std::string)it.value();
        }
    }
}