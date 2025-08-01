  ## URL职责分离架构图
```bash
  ┌────────────────────────────────────────────────────────────────
  ─┐
  │                    Application Layer
   │
  │                   (业务逻辑处理)
   │
  └────────────────────────────────────────────────────────────────
  ─┘
                                   │
  ┌────────────────────────────────────────────────────────────────
  ─┐
  │              infra-controller (channel.h)
  │
  │                    业务通信模式
  │
  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
   │
  │  │ publisher_url_  │  │ subscriber_url  │  │ output_url_     │
   │
  │  │ (1对多广播)     │  │ (动态订阅)     │  │ (输出重定向)    │
  │
  │  │ 业务层决定      │  │ 运行时构建     │  │ 业务层配置      │
  │
  │  └─────────────────┘  └─────────────────┘  └─────────────────┘
   │
  └────────────────────────────────────────────────────────────────
  ─┘
                                   │
  ┌────────────────────────────────────────────────────────────────
  ─┐
  │               unit-manager (unit_data.h)
  │
  │                    资源分配管理
  │
  │  ┌─────────────────┐              ┌─────────────────┐
  │
  │  │ inference_url   │              │ output_url      │
  │
  │  │ (推理端点)      │              │ (输出端点)      │
  │
  │  │ 需要端口分配    │              │ 需要端口分配    │
  │
  │  │ 资源层管理      │              │ 资源层管理      │
  │
  │  └─────────────────┘              └─────────────────┘
  │
  └────────────────────────────────────────────────────────────────
  ─┘
                                   │
  ┌────────────────────────────────────────────────────────────────
  ─┐
  │                     Network Layer
  │
  │                   (实际网络通信)
  │
  └────────────────────────────────────────────────────────────────
  ─┘
```


## 这种设计遵循了关注点分离原则：
unit-manager专注资源管理，
infra-controller专注业务通信模式。


## 关键差异

subscriber_url的特殊性

- 局部变量: 在subscriber_work_id()函数中动态构建
- 数据来源: 通过RPC调用unit_call("sys", "sql_select", 
input_url_name)从unit-manager获取
- 构建逻辑:
if (work_id匹配格式) {
    subscriber_url = 从unit_data.output_url获取;  // 第71行
} else {
    subscriber_url = inference_url_;             // 第77行  
}


## 职责分离总结

1. unit-manager: 分配基础通信资源(需要端口管理)
2. infra-controller: 构建业务通信模式(发布订阅、广播等)
3. subscriber_url: 桥接层，运行时从资源层获取URL用于业务层


## 完整的通信图

```bash
                      ┌─────────────┐
                      │  LLM Unit   │
                      │             │
  inference_url ─────→│  [处理器]   │─────→ output_url
  "tcp://:5555"       │             │       "tcp://:5556"
       ↑              └─────────────┘            ↓
       │                                        │
  ┌─────────┐                              ┌─────────┐
  │ Client  │                              │Subscriber│
  │(发送请求)│                              │(接收结果)│
  └─────────┘                              └─────────┘
                                                ↑
                                    subscriber_url = output_url
```

## Subscriber接收结果后的处理流程

  1. 关键发现

  从代码分析可以看出，subscriber接收到LLM结果后会：

  1. 提取zmq_com:
  从LLM返回的消息中提取zmq_com字段(channel.cpp:36-38)
  2. 设置推送URL: 调用set_push_url(zmq_com)设置返回给Client的URL
  3. 调用业务回调: 执行传入的callback函数处理结果
  4. 发送给Client: 通过send_raw_to_usr()将结果推送给Client

  2. 完整通信流程图
```bash
  ┌─────────┐  1.推理请求    ┌──────────────┐  2.处理请求
  ┌─────────┐
  │ Client  │ ──────────→   │unit-manager  │ ─────────→   │LLM Unit
   │
  │         │   +zmq_com    │ (TCP服务器)  │              │        
   │
  └─────────┘               └──────────────┘              
  └─────────┘
       ↑                                                       │
       │                                                  
  3.发布结果
       │                                                       ↓
       │                                              
  ┌─────────────┐
       │                                              │ output_url
   │
       │                                              │(ZMQ_PUB)
   │
       │
  └─────────────┘
       │                                                       │
       │ 6.返回结果                                          4.订阅
       │ (zmq_com URL)                                        │
       │                                                       ↓
  ┌─────────────────┐  5.处理&转发   ┌──────────────────────────┐
  │ infra-controller│ ←────────────  │    subscriber_url        │
  │ (llm_channel)   │                │ = LLM_Unit.output_url    │
  └─────────────────┘                └──────────────────────────┘
```
  3. zmq_com的作用

  - Client发送请求时: 包含自己的接收URL(zmq_com)
  - unit-manager转发: 将zmq_com嵌入到推理请求中
  - LLM处理完成: 结果中携带原始的zmq_com
  - subscriber接收: 提取zmq_com并用它发送结果给Client

  4. 代码验证

  // 1. subscriber_event_call提取zmq_com
  std::string zmq_com = sample_json_str_get(_raw, "zmq_com");
  if (!zmq_com.empty())
      set_push_url(zmq_com);  // 设置Client的返回地址

  // 2. set_push_url创建推送连接
  zmq_[-2].reset(new pzmq(output_url_, ZMQ_PUSH));  // 
  output_url_=zmq_com

  // 3. send方法中返回结果
  send_raw_to_usr(out);  // 推送给Client

  总结:
  subscriber通过提取LLM结果中的zmq_com字段，建立到Client的返回通道
  ，实现了完整的请求-响应循环。这是一个非常巧妙的异步通信设计！



## 那为什么不在LLM模块推理完成后,直接返回给client,为什么一定要通过业务层的subscriber_url订阅,再返回呢

● 这是一个非常深刻的架构问题！让我分析为什么要采用这种看似"绕弯"的
  设计：

  为什么不直接返回给Client？

  1. 解耦合设计
```bash
  直接返回模式 (紧耦合):
  Client ←────────→ LLM Unit
        请求/响应   (直接连接)

  现有模式 (松耦合):
  Client → unit-manager → LLM Unit → output_url
    ↑                                     ↓
    └──── infra-controller ←──── subscriber
          (业务处理层)
```
  2. 核心原因分析

  A. 业务逻辑处理需求

  LLM的原始输出通常需要额外处理：
  - 格式转换: 将模型输出转换为标准API格式
  - 流式处理: 支持streaming响应
  - 错误处理: 统一的错误码和错误信息
  - 日志记录: 请求追踪、性能监控
  - 结果过滤: 内容安全、敏感信息过滤

  B. 多播和路由需求

  从channel.h:127-130可以看到：
  send_raw_to_pub(out);    // 发布给所有订阅者
  if (enoutput_) {
      return send_raw_to_usr(out);  // 发送给原始Client
  }

  一个LLM结果可能需要：
  - 发送给原始Client
  - 广播给监控系统
  - 记录到日志服务
  - 缓存到结果存储

  C. 异步处理能力

  同步模式: Client等待 → LLM处理 → 直接返回
  异步模式: Client发送 → 立即返回 → 后台处理 → 通知Client

  subscriber模式支持真正的异步处理。

  D. 可扩展性和容错

  - 负载均衡: 多个infra-controller可以订阅同一个LLM的输出
  - 故障恢复: 如果infra-controller挂掉，可以快速切换到备用节点
  - A/B测试: 不同版本的业务逻辑可以同时订阅同一个LLM

  3. 实际应用场景

  流式对话系统

  // LLM生成token流
  LLM: "Hello" → "Hello world" → "Hello world, I"...

  // 业务层处理每个token
  subscriber接收 → 格式化 → 实时推送给Client → 显示打字效果

  多模态处理

  // LLM输出可能包含文本+图片+代码
  LLM原始输出: {"text": "...", "image_ref": "...", "code": "..."}

  // 业务层分别处理
  subscriber → 提取图片引用 → 生成图片URL → 格式化响应 →
  发送给Client

  4. 架构优势总结
```bash
  | 直接返回模式 | Subscriber模式 |
  |--------|--------------|
  | 简单直接   | 支持复杂业务逻辑     |
  | 紧耦合    | 松耦合，可扩展      |
  | 单一响应   | 支持多播、流式      |
  | 同步阻塞   | 异步非阻塞        |
  | 难以监控   | 易于监控和调试      |
```
  5. 类比现实场景

  就像餐厅点餐系统：
  - 直接模式: 顾客直接找厨师要菜 (不现实)
  - Subscriber模式: 顾客 → 服务员 → 厨师 → 传菜员 → 服务员 → 顾客

  传菜员(subscriber)的作用：
  - 检查菜品质量
  - 添加配菜和装饰
  - 确保温度适宜
  - 记录服务日志

  这种设计虽然复杂，但为系统提供了业务处理能力、可扩展性和容错性，
  这在企业级LLM服务中是必不可少的。


## URL关系架构图
```bash
  ┌─────────────────────────────────────────────────────────────────────────────┐
  │                            unit-manager (资源分配层)                        │
  ├─────────────────────────────────────────────────────────────────────────────┤
  │                                                                             │
  │  inference_url                          output_url                         │
  │  ┌─────────────────────┐                ┌─────────────────────┐             │
  │  │ ✅ 实际ZMQ_PUB连接   │                │ ❌ 仅配置占位符      │             │
  │  │ ✅ send_msg()发布   │                │ ❌ 无ZMQ连接        │             │
  │  │ ✅ LLM结果输出      │                │ ❌ 未实际使用       │             │
  │  └─────────────────────┘                └─────────────────────┘             │
  │           │                                       │                         │
  │           │ 配置传递+实际通信                      │ 仅配置传递               │
  │           │                                       │                         │
  └───────────┼───────────────────────────────────────┼─────────────────────────┘
              │                                       │
              ▼                                       ▼
  ┌─────────────────────────────────────────────────────────────────────────────┐
  │                       infra-controller (业务通信层)                         │
  ├─────────────────────────────────────────────────────────────────────────────┤
  │                                                                             │
  │  publisher_url_          inference_url_                                     │
  │  ┌─────────────────┐    ┌─────────────────┐                                │
  │  │ zmq_[-1]        │    │ 仅配置存储      │                                │
  │  │ ZMQ_PUB         │    │ fallback用途    │                                │
  │  │ 广播给多个系统   │    │ 无ZMQ连接       │                                │
  │  └─────────────────┘    └─────────────────┘                                │
  │                                                                             │
  │  output_url_             subscriber_url                                     │
  │  ┌─────────────────┐    ┌─────────────────┐                                │
  │  │ zmq_[-2]        │    │ zmq_[id_num]    │                                │
  │  │ ZMQ_PUSH        │    │ ZMQ_SUB         │                                │
  │  │ 推送给Client    │    │ 订阅LLM结果     │                                │
  │  └─────────────────┘    └─────────────────┘                                │
  │         ▲                        ▲                                         │
  │         │                        │                                         │
  │   动态设置Client地址        动态选择订阅地址                                │
  │   (zmq_com)               (output_url或inference_url_)                     │
  │                                                                             │
  └─────────────────────────────────────────────────────────────────────────────┘
```

```bash
  数据流向关系:
  ════════════

  unit_data.inference_url  ════════════════════════════════════════════════════╗
      (ZMQ_PUB实际发布)                                                       ║
                                                                              ▼
                                                                  subscriber_url
                                                                 (ZMQ_SUB订阅接收)
                                                                              ║
                                                                              ▼
  unit_data.output_url  ══════════════════════════════════════════════════════╬═════╗
      (仅提供配置地址)                                                        ║     ║
                                                              subscriber_url选择逻辑  ║
                                                                if (有work_id)      ║
                                                                              ║     ║
  Client.zmq_com  ════════════════════════════════════════════════════════════╬═════╬═══╗
      (Client返回地址)                                                        ║     ║   ║
                                                                              ▼     ║   ▼
                                                                  infra-controller  ║ output_url_
                                                                     业务处理       ║ (推送Client)
                                                                              ║     ║
                                                                              ▼     ║
                                                                      publisher_url_ ║
                                                                     (广播其他系统)  ║
                                                                                    ║
  unit_data.inference_url  ══════════════════════════════════════════════════════════╬═══╗
      (作为配置传递)                                                                 ║   ║
                                                                                    ▼   ▼
                                                                          inference_url_
                                                                         (fallback配置)
```

```bash
  实际通信流程:
  ═════════════

  Client请求 ──→ unit-manager ──→ LLM处理 ──→ send_msg()
                                              │
                                              ▼
                                     inference_url (ZMQ_PUB)
                                              │
                                              ▼
                                     subscriber_url (ZMQ_SUB)
                                              │
                                              ▼
                                     infra-controller处理
                                          │       │
                            ┌─────────────┘       └─────────────┐
                            ▼                                   ▼
                   publisher_url_                      output_url_
                  (广播监控/日志)                      (推送Client)
                            │                                   │
                            ▼                                   ▼
                      其他订阅系统                         Client接收
```
  关键要点

  实际使用情况

  - unit_data.inference_url: 既有实际ZMQ连接，又传递配置
  - unit_data.output_url: 纯配置传递，无实际功能
  - subscriber_url: 动态构建，实际订阅unit_data.inference_url
  - output_url_: 动态设置为Client的zmq_com地址

  设计问题

  - inference_url身兼数职：实际发布端点 + 配置传递
  - output_url在unit-manager中是"占位符"
  - 命名与实际功能不完全匹配

## 现在我理解了！让我画出unit-manager和infra-controller之间的关联架构图：

  unit-manager与infra-controller关联架构图
```bash
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                              完整系统架构                                       │
  └─────────────────────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                          unit-manager (独立进程)                                │
  │                                                                                 │
  │  ┌─────────────────────────────────────────────────────────────────────────────┐ │
  │  │                        RPC服务器 (sys_rpc_server_)                         │ │
  │  │                                                                             │ │
  │  │  监听地址: ipc:///tmp/rpc.sys                                                │ │
  │  │                                                                             │ │
  │  │  注册的RPC方法:                                                              │ │
  │  │  ├─ "sql_select"    ──→  rpc_sql_select()                                  │ │
  │  │  ├─ "register_unit" ──→  rpc_allocate_unit()                               │ │
  │  │  ├─ "release_unit"  ──→  rpc_release_unit()                                │ │
  │  │  ├─ "sql_set"       ──→  rpc_sql_set()                                     │ │
  │  │  └─ "sql_unset"     ──→  rpc_sql_unset()                                   │ │
  │  │                                                                             │ │
  │  └─────────────────────────────────────────────────────────────────────────────┘ │
  │                                                                                 │
  │  全局资源管理:                                                                   │
  │  ├─ key_sql (配置存储)                                                           │
  │  ├─ port_list (端口分配)                                                         │
  │  └─ unit_data实例 (单元管理)                                                     │
  │                                                                                 │
  └─────────────────────────────────────────────────────────────────────────────────┘
                                      ▲
                                      │ RPC调用
                                      │ ipc:///tmp/rpc.sys
                                      │
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                      infra-controller (StackFlow类)                            │
  │                                                                                 │
  │  每个StackFlow实例包含:                                                          │
  │  ┌─────────────────────────────────────────────────────────────────────────────┐ │
  │  │                    unit_call()函数调用                                     │ │
  │  │                                                                             │ │
  │  │  unit_call("sys", "register_unit", unit_name)                              │ │
  │  │  ┌─────────────────────────────────────────────────────────────────────────┐ │ │
  │  │  │  pzmq _call("sys");  // 创建RPC客户端                                   │ │ │
  │  │  │  _call.call_rpc_action(action, data, callback);                        │ │ │
  │  │  │                                                                         │ │ │
  │  │  │  实际连接: ipc:///tmp/rpc.sys                                            │ │ │
  │  │  └─────────────────────────────────────────────────────────────────────────┘ │ │
  │  │                                                                             │ │
  │  │  其他RPC调用:                                                                │ │
  │  │  ├─ unit_call("sys", "sql_select", key)                                    │ │
  │  │  ├─ unit_call("sys", "release_unit", work_id)                              │ │
  │  │  ├─ unit_call("sys", "sql_set", json)                                      │ │
  │  │  └─ unit_call("sys", "sql_unset", key)                                     │ │
  │  │                                                                             │ │
  │  └─────────────────────────────────────────────────────────────────────────────┘ │
  │                                                                                 │
  │  本地资源管理:                                                                   │
  │  └─ llm_task_channel_ (llm_channel_obj实例管理)                                 │
  │                                                                                 │
  └─────────────────────────────────────────────────────────────────────────────────┘
```

  具体的调用流程:
  ═══════════════
```bash
  1. infra-controller请求注册单元:
     StackFlow::sys_register_unit() 
     └─→ unit_call("sys", "register_unit", unit_name)
         └─→ pzmq("sys") RPC调用
             └─→ 连接 ipc:///tmp/rpc.sys
                 └─→ unit-manager::rpc_allocate_unit()
                     └─→ sys_allocate_unit()
                         └─→ 分配unit_data实例
                             └─→ 返回: work_id + output_url + inference_url

  2. infra-controller创建channel:
     llm_task_channel_[work_id] =
     └─→ std::make_shared<llm_channel_obj>(out_port, inference_port, unit_name)

  3. infra-controller查询配置:
     channel::subscriber_work_id()
     └─→ unit_call("sys", "sql_select", work_id + ".out_port")
         └─→ 获取unit_data的output_url用于订阅
```

  关键要点:
  ════════

  ✅ unit-manager是独立进程，通过RPC服务器对外提供服务
  ✅ infra-controller是库/类，多个实例可以同时连接unit-manager
  ✅ 没有unit-manager的实例化对象，而是通过RPC进行进程间通信
  ✅ 每个infra-controller的StackFlow实例都可以独立管理自己的llm_channel
  ✅ unit-manager负责全局资源分配，infra-controller负责业务逻辑处理

  总结

  回答你的问题：

  1. 关联方式: 通过RPC通信，不是对象实例化
  2. 没有unit-manager实例化对象: unit-manager是独立进程，通过ZMQ RPC服务器对外提供服务
  3. 通信协议: 使用ipc:///tmp/rpc.sys进行进程间通信
  4. 服务架构: unit-manager提供资源管理服务，infra-controller通过unit_call()函数调用这些服务

  这是一个典型的微服务架构，通过RPC实现服务解耦！

## infra-controller与channel的关系架构
```bash
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                    infra-controller与channel关系                                │
  │                          (一对多关系)                                           │
  └─────────────────────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                      一个infra-controller实例                                  │
  │                          (StackFlow对象)                                       │
  │                                                                                 │
  │  unit_name_: "llm_service"                                                     │
  │                                                                                 │
  │  ┌───────────────────────────────────────────────────────────────────────────┐  │
  │  │              llm_task_channel_ (哈希表管理)                               │  │
  │  │                                                                           │  │
  │  │  std::unordered_map<int, std::shared_ptr<llm_channel_obj>>                │  │
  │  │                                                                           │  │
  │  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐           │  │
  │  │  │ channel[1001]   │  │ channel[1002]   │  │ channel[1003]   │           │  │
  │  │  │ work_id:        │  │ work_id:        │  │ work_id:        │           │  │
  │  │  │ "llm_service.1" │  │ "llm_service.2" │  │ "llm_service.3" │           │  │
  │  │  │                 │  │                 │  │                 │           │  │
  │  │  │ publisher_url_  │  │ publisher_url_  │  │ publisher_url_  │           │  │
  │  │  │ inference_url_  │  │ inference_url_  │  │ inference_url_  │           │  │
  │  │  │ output_url_     │  │ output_url_     │  │ output_url_     │           │  │
  │  │  │ subscriber连接  │  │ subscriber连接  │  │ subscriber连接  │           │  │
  │  │  └─────────────────┘  └─────────────────┘  └─────────────────┘           │  │
  │  │           ▲                   ▲                   ▲                       │  │
  │  │           │                   │                   │                       │  │
  │  │      每次setup()调用      每次setup()调用      每次setup()调用           │  │
  │  │      创建新channel        创建新channel        创建新channel             │  │
  │  │                                                                           │  │
  │  └───────────────────────────────────────────────────────────────────────────┘  │
  │                                                                                 │
  └─────────────────────────────────────────────────────────────────────────────────┘
                                    ▲
                             多次setup()调用
                                    │
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                            外部请求来源                                          │
  │                                                                                 │
  │  请求1: setup("llm_service", task_config_1)  ──→  创建channel[1001]            │
  │  请求2: setup("llm_service", task_config_2)  ──→  创建channel[1002]            │
  │  请求3: setup("llm_service", task_config_3)  ──→  创建channel[1003]            │
  │  ...                                                                           │
  │                                                                                 │
  │  每个channel对应一个独立的LLM任务实例                                            │
  │                                                                                 │
  └─────────────────────────────────────────────────────────────────────────────────┘
```

  channel创建流程:
  ═══════════════
```bash
  1. 外部调用 StackFlow::setup(zmq_url, raw)
     └─→ sys_register_unit(unit_name_)  // 向unit-manager注册新单元
         └─→ unit_call("sys", "register_unit", "llm_service")
             └─→ unit-manager分配: work_id=1001, output_url, inference_url
                 └─→ llm_task_channel_[1001] =
                     new llm_channel_obj(output_url, inference_url, "llm_service")

  2. 再次调用 StackFlow::setup(zmq_url, raw)  
     └─→ sys_register_unit(unit_name_)  // 再次注册(新的单元实例)
         └─→ unit-manager分配: work_id=1002, output_url, inference_url
             └─→ llm_task_channel_[1002] =
                 new llm_channel_obj(output_url, inference_url, "llm_service")
```

  实际使用场景:
  ═════════════
```bash
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                          多任务处理示例                                         │
  │                                                                                 │
  │  一个LLM服务实例 (infra-controller)                                              │
  │  ├─ channel[1001]: 处理用户A的对话任务                                           │
  │  ├─ channel[1002]: 处理用户B的翻译任务                                           │
  │  ├─ channel[1003]: 处理用户C的代码生成任务                                       │
  │  └─ channel[1004]: 处理用户D的文档摘要任务                                       │
  │                                                                                 │
  │  每个channel独立管理:                                                            │
  │  ├─ 独立的LLM模型配置                                                            │
  │  ├─ 独立的通信连接                                                              │
  │  ├─ 独立的请求处理                                                              │
  │  └─ 独立的生命周期管理                                                          │
  │                                                                                 │
  └─────────────────────────────────────────────────────────────────────────────────┘
```
  总结

  回答你的问题：

  一个infra-controller ↔ 多个channel

  1. 1:N关系: 一个infra-controller(StackFlow实例)可以管理多个channel
  2. 动态创建: 每次setup()调用都会创建一个新的channel
  3. 任务隔离: 每个channel对应一个独立的LLM任务实例
  4. 资源共享: 所有channel共享同一个unit-manager资源池

  关键要点:

  - infra-controller: 是服务容器，管理多个LLM任务
  - channel: 是任务实例，每个代表一个具体的LLM处理任务
  - 扩展性: 支持一个服务同时处理多个不同的LLM任务
  - 隔离性: 每个channel有独立的配置、连接和生命周期

  这种设计支持多租户和多任务并发处理！


## unit-manager与infra-controller连接机制
```bash
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                        连接和调用机制详解                                        │
  └─────────────────────────────────────────────────────────────────────────────────┘

  连接协议: ZMQ IPC (进程间通信)
  ═══════════════════════════════

  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                          unit-manager (服务端)                                 │
  │                                                                                 │
  │  RPC服务器启动:                                                                  │
  │  ┌───────────────────────────────────────────────────────────────────────────┐  │
  │  │  sys_rpc_server_ = std::make_unique<pzmq>("sys");                        │  │
  │  │                                                                           │  │
  │  │  实际绑定地址: ipc:///tmp/rpc.sys                                          │  │
  │  │  ZMQ模式: ZMQ_REP (Reply - 应答者)                                        │  │
  │  │  监听状态: 等待客户端连接和请求                                             │  │
  │  │                                                                           │  │
  │  │  注册的RPC方法:                                                            │  │
  │  │  ├─ "sql_select"    → rpc_sql_select()                                   │  │
  │  │  ├─ "register_unit" → rpc_allocate_unit()                                │  │
  │  │  ├─ "release_unit"  → rpc_release_unit()                                 │  │
  │  │  ├─ "sql_set"       → rpc_sql_set()                                      │  │
  │  │  └─ "sql_unset"     → rpc_sql_unset()                                    │  │
  │  │                                                                           │  │
  │  └───────────────────────────────────────────────────────────────────────────┘  │
  └─────────────────────────────────────────────────────────────────────────────────┘
                                      ▲
                                      │
                              ZMQ REQ-REP 协议
                            ipc:///tmp/rpc.sys socket
                                      │
                                      ▼
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                       infra-controller (客户端)                                │
  │                                                                                 │
  │  RPC客户端调用:                                                                  │
  │  ┌───────────────────────────────────────────────────────────────────────────┐  │
  │  │  // StackFlowUtil.cpp:162                                                │  │
  │  │  pzmq _call(unit_name);  // unit_name = "sys"                            │  │
  │  │                                                                           │  │
  │  │  连接过程:                                                                 │  │
  │  │  ┌─────────────────────────────────────────────────────────────────────┐  │  │
  │  │  │  1. 创建ZMQ_REQ客户端                                                │  │  │
  │  │  │  2. 拼接地址: rpc_url_head_ + unit_name                              │  │  │
  │  │  │     = "ipc:///tmp/rpc." + "sys"                                     │  │  │
  │  │  │     = "ipc:///tmp/rpc.sys"                                          │  │  │
  │  │  │  3. 连接到unit-manager服务器                                         │  │  │
  │  │  │  4. 发送RPC请求                                                      │  │  │
  │  │  │  5. 等待响应                                                         │  │  │
  │  │  └─────────────────────────────────────────────────────────────────────┘  │  │
  │  │                                                                           │  │
  │  │  调用示例:                                                                 │  │
  │  │  _call.call_rpc_action("register_unit", "llm_service", callback);        │  │
  │  │                                                                           │  │
  │  └───────────────────────────────────────────────────────────────────────────┘  │
  └─────────────────────────────────────────────────────────────────────────────────┘
```

  具体连接细节:
  ═════════════

  1. 物理连接方式:
  ```bash
     ┌─────────────────────────────────────────────────────────────────────────────┐
     │  IPC Socket 文件: /tmp/rpc.sys                                             │
     │                                                                             │
     │  - 类型: Unix Domain Socket                                                │
     │  - 协议: ZMQ REQ-REP 模式                                                   │
     │  - 特点: 同主机进程间通信，高性能，低延迟                                    │
     │  - 权限: 文件系统权限控制                                                    │
     └─────────────────────────────────────────────────────────────────────────────┘
  ```

  2. 请求-响应流程:
  ```bash
     ┌─────────────────────────────────────────────────────────────────────────────┐
     │                                                                             │
     │  infra-controller                     unit-manager                         │
     │  ┌─────────────────┐                 ┌─────────────────┐                   │
     │  │                 │    RPC请求      │                 │                   │
     │  │ unit_call("sys",│ ──────────────→ │ sys_rpc_server_ │                   │
     │  │ "register_unit",│                 │                 │                   │
     │  │ "llm_service")  │                 │ 解析请求        │                   │
     │  │                 │                 │ 调用handler     │                   │
     │  │ 等待响应        │                 │ 执行业务逻辑     │                   │
     │  │                 │    RPC响应      │                 │                   │
     │  │ 处理返回结果    │ ←────────────── │ 返回结果        │                   │
     │  │                 │                 │                 │                   │
     │  └─────────────────┘                 └─────────────────┘                   │
     │                                                                             │
     └─────────────────────────────────────────────────────────────────────────────┘
  ```

  3. 消息格式:
  ```bash
     ┌─────────────────────────────────────────────────────────────────────────────┐
     │  请求格式: JSON-RPC风格                                                      │
     │  {                                                                          │
     │    "method": "register_unit",                                               │
     │    "params": "llm_service"                                                  │
     │  }                                                                          │
     │                                                                             │
     │  响应格式: 序列化数据                                                        │
     │  "1001|tcp://localhost:5001|tcp://localhost:5002"                          │
     │  (work_id|output_url|inference_url)                                        │
     └─────────────────────────────────────────────────────────────────────────────┘
  ```

  地址解析逻辑:
  ═════════════

  // pzmq.hpp:56 构造函数
  pzmq(const std::string &server) : rpc_server_(server) {
      if (server.find("://") != std::string::npos) {
          rpc_url_head_.clear();  // server已包含完整URL
      }
      // 否则使用默认前缀: "ipc:///tmp/rpc."
  }

  // 实际连接时
  std::string full_url = rpc_url_head_ + rpc_server_;
  // = "ipc:///tmp/rpc." + "sys"
  // = "ipc:///tmp/rpc.sys"

  zmq_connect(zmq_socket_, full_url.c_str());


  启动顺序要求:
  ═════════════
```bash
  1. unit-manager 必须先启动
     ├─ 创建 /tmp/rpc.sys socket文件
     ├─ 绑定并监听连接
     └─ 等待客户端连接

  2. infra-controller 后启动
     ├─ 连接到已存在的socket文件
     ├─ 发送RPC请求
     └─ 接收处理结果

  3. 故障处理
     ├─ 如果socket文件不存在，连接失败
     ├─ unit-manager重启会重新创建socket
     └─ infra-controller有超时和重试机制
```
  总结

  unit-manager与infra-controller通过以下方式连接：

  连接协议

  - ZMQ IPC: Unix Domain Socket进程间通信
  - 地址: ipc:///tmp/rpc.sys
  - 模式: REQ-REP (请求-应答)

  调用机制

  - 客户端: infra-controller通过unit_call()函数
  - 服务端: unit-manager的RPC服务器接收处理
  - 数据格式: JSON和序列化字符串

  关键特点

  - 高性能: IPC比TCP更快，适合同主机通信
  - 可靠性: REQ-REP模式保证请求-响应一致性
  - 扩展性: 支持多个infra-controller同时连接
  - 简单性: 通过文件系统socket，无需网络配置

  这是一个典型的分布式RPC架构，实现了服务间的松耦合通信！