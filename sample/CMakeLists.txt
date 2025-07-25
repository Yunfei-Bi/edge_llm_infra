cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置构建类型为 debug
set(CMAKE_BUILD_TYPE Debug)

include_directories("../hybird-comm/include/")
include_directories("../hybird-comm/include/libzmq")

add_executable(zmq_pub pub.cc ../hybird-comm/src/pzmq_data.cpp)
target_link_libraries(zmq_pub
    zmq
    pthread
)

add_executable(zmq_sub
    sub.cc
    ../hybird-comm/src/pzmq_data.cpp
)
target_link_lbraries(zmq_sub
    zmq
    pthread
)

add_executable(rpc_client
    rpc_call.cc
    ../hybird-comm/src/pzmq_data.cpp
)
target_link_libraries(rpc_client
    zmq
    pthread
)

add_executable(rpc_server
    rpc_server.cc
    ../hybird-comm/src/pzmq_data.cpp
)
target_link_libraries(rpc_server
    zmq
    pthread
)

add_executable(pz_rpc_client
    pz_rpc_call.cc
    ../hybird-comm/src/pzmq_data.cpp
)
target_link_libraries(pz_rpc_client
    zmq
    pthread
)

add_executable(pz_rpc_server
    pz_rpc_server.cc
    ../hybird-comm/src/pzmq_data.cpp
)
target_link_libraries(pz_rpc_server
    zmq
    pthread
)
