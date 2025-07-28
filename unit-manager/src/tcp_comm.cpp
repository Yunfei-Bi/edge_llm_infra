#include <unordered_map>
#include <unistd.h>
#include <chrono>
#include <any>
#include <cstring>
#include <iostream>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <boost/any.hpp>
#include <variant>

#include "all.h"
#include "session.h"
#include "zmq_bus.h"
#include "json.hpp"

std::atomic<int> counter_port(8000);
network::EventLoop loop;
std::unique_ptr<netwrok::TcpServer> server;
std::mutex context_mutex;

void onConnection(const network::TcpConnectionPtr &conn) {
    if (conn->connected()) {
        std::shared_ptr<TcpSession> session = std::make_shared<TcpSession>(conn);
        conn->setContext(session);
        session->work(zmq_s_format, counter_port.fetch_add(1));

        if (counter_port > 65535) {
            counter_port = 8000;
        }
    } else {
        try {
            auto session = boost::any_cast<std::shared_ptr<TcpSession>>(conn->getContext());
            session->stop();
        } catch (const std::bad_any_cast &e) {
            std::cerr << "Bad ant_cast: " << e.what() << std::endl;
        }
    }
}

void onMessage (const network::TcpConnectionPtr &conn, network::Buffer &buf) {
    std::string msg(buf->retrieveAllAsString());

    try {
        auto session = boost::any_cast<std::shared_ptr<TcpSession>>(conn->getContext());
        session->select_json_str(msg, std::bind(&TcpSession::on_data, session, 
                                std::placeholders::_1));
    } catch (const boost::bad_any_cast &e) {
        std::cerr << "Type cast error: " << e.what() << std::endl;
    }
}

void tcp_work() {
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

void tcp_stop_work() {
    loop.quit();
    server.reset();
}