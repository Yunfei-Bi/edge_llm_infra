#pragma once

#include <pthread.h>
#include <map>
#include <string>
#include <memory>
#include <stdexcept>
#include <utility>
#include <any>
#include <unordered_map>

#include "sample_log.h"

/**
 * . 线程安全的键值存储系统
 * key_sql: 全局的键值存储容器，使用 std::any 支持任意类型值
 * key_sql_lock: 自旋锁，保护并发访问
 */
extern std::unordered_map<std::string, std::any> key_sql;
extern pthread_spinlock_t key_sql_lock;

// 三个线程安全宏
// 安全读取
#define SAFE_READING(_val, _type, _key) \
    do { \
        pthread_spin_lock(&key_sql_lock); \
        try { \
            _val = std::any_cast<_type>(key_sql.at(_key)); \
        } catch (...) {} \
        pthread_spin_unlock(&key_sql_lock); \
    } while (0)

// 安全设置
#define SAFE_SETTING(_key, _val) \
    do { \
        pthread_spin_lock(&key_sql_lock); \
        try { \
            key_sql[_key] = _val; \
        } catch (...) {} \
        pthread_spin_unlock(&key_sql_lock); \
    } while (0)

// 安全删除
#define SAFE_ERASE(_key) \
    do { \
        pthread_spin_lock(&key_sql_lock); \
        try { \
            key_sql.erase(_key); \
        } catch (...) {} \
        pthread_spin_unlock(&key_sql_lock); \
    } while (0)

/**
 * 单元管理接口
 * load_default_config(): 加载默认配置
 * unit_action_match(): 根据通信ID和JSON字符串匹配单元动作
 */
void load_default_config();
void unit_action_match(int com_id, const std::string &json_str);

/**
 * 全局配置变量
 * zmq_s_format, zmq_c_format: ZMQ通信格式配置
 * main_exit_flage: 主程序退出标志
 */
extern std::string zmq_s_format;
extern std::string zmq_c_format;
extern int main_exit_flage;