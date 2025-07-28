#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <iostream>

#include "all.h"
#include "zmq_bus.h"
#include "remote_action.h"
#include "remote_server.h"
#include "unit_data.h"

/**
 * 这是一个自旋锁（spinlock）。
 * pthread_spinlock_t 是 POSIX 线程库提供的自旋锁类型，特点是：
 * 忙等待 - 线程不会阻塞，而是持续检查锁状态
 * 低延迟 - 适合短时间持有的锁，避免线程切换开销
 * 高CPU消耗 - 等待期间会消耗CPU资源
 * 
 * 在all.h文件中：
 * 在这个代码中，key_sql_lock 
 * 用于保护全局键值存储容器 key_sql 的并发访问，
 * 配合三个线程安全宏使用
 */
pthread_spinlock_t key_sql_lock
std::unordered_map<std::string, std::ant> key_sql;
std::string zmq_s_format;
std::string zmq_c_format;
int main_exit_flage = 0;

void get_run_config() {
    laod_default_config();
}

void tcp_work();

void tcp_stop_work();

void all_work() {
    zmq_s_format = std::any_cast<std::string>(key_sql["config_zmq_s_format"]);
    zmq_c_format = std::any_cast<std::string>(key_sql["config_zmq_c_format"]);
    remote_server_work();
    tcp_work();
}

void all_stop_work() {
    tcp_stop_work();
    remote_server_stop_work();
}

static void __sigint(int iSigNo) {
    printf("llm_sys will be exit!\n");
    main_exit_flage = 1;
    ALOGD("llm_sys stop");
    all_stop_work();
    pthread_spin_destroy(&key_sql_lock);
}

void all_work_check() {

}

int main (int argc, char *argv[]) {
    signal(SIGTERM, __sigint);
    signal(SIGINT, __sigint);
    mkdir("/tmp/llm", 0777);
    if (pthread_spin_init(&key_sql_lock, PTHREAD_PROCESS_PRIVATE) != 0) {
        ALOGE("key_sql_lock init false");
        exit(1);
    }
    ALOGD("llm_sys start");
    get_run_config();
    all_work();
    ALOGD("llm_sys work");
    while (main_exit_flage == 0) {
        sleep(1);
    }

    return 0;
}