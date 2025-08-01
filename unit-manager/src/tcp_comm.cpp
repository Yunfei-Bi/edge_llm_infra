
#include <unordered_map>
#include <unistd.h>
#include <chrono>
#include <any>
#include <cstring>
#include <ctime>
#include <iostream>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "all.h"
#include "session.h"
#include "zmq_bus.h"
#include <boost/any.hpp>
#include "json.hpp"
#include <variant>

std::atomic<int> counter_port(8000);
network::EventLoop loop;
std::unique_ptr<network::TcpServer> server;
std::mutex context_mutex;

// 当有新的 TCP 连接时，创建一个新的 TcpSession 对象，并将其与连接关联。如果连接断开，则尝试获取并停止对应的 TcpSession 对象
void onConnection(const network::TcpConnectionPtr &conn) {
    if (conn->connected()) {
        std::shared_ptr<TcpSession> session = std::make_shared<TcpSession>(conn);
        conn->setContext(session);
        session->work(zmq_s_format, counter_port.fetch_add(1));

        if (counter_port > 65535) {
            counter_port.store(8000);
        }
    }
    else {
        try {
            auto session = boost::any_cast<std::shared_ptr<TcpSession>>(conn->getContext());
            session->stop();
        } catch (const std::bad_any_cast &e) {
            std::cerr << "Bad any_cast: " << e.what() << std::endl;
        }
    }
}


// onMessage：当接收到 TCP 消息时，将消息内容传递给对应的 TcpSession 对象进行处理
void onMessage(const network::TcpConnectionPtr &conn,
                network::Buffer *buf)
{
    std::string msg(buf->retrieveAllString());
    try {
        auto session = boost::any_cast<std::shared_ptr<TcpSession>>(conn->getContext());
        session->select_json_str(msg, std::bind(&TcpSession::on_data, session, std::placeholders::_1));
    } catch (const boost::bad_any_cast &e) {
        std::cerr << "Type cast error: " << e.what() << std::endl;
    }
}

// tcp_work：初始化并启动 TCP 服务器，监听配置文件中指定的端口，并设置连接和消息回调函数。
void tcp_work()
{
    int listenport = 0;
    SAFE_READING(listenport, int, "config_tcp_server");
    network::InetAddress listenAddr(listenport);
    server = std::make_unique<network::TcpServer>(&loop, listenAddr, "ZMQBridge");

    server->setConnectionCallback(onConnection);
    server->setMessageCallback(onMessage);
    server->setThreadNum(2);

    server->start();
    loop.loop();
}


void tcp_stop_work()
{
    loop.quit();
    server.reset();
}