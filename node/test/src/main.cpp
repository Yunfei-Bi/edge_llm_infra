#include "StackFlow.h"
#include "channel.h"
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <stdexcept>
#include <iostream>

using namespace StackFlows;
using json = nlohmann::json;

int main_exit_flag = 0;
static void __isgint(int iSigNo)
{
    main_exit_flage = 1;
}

typedef std::function<void(const std::string &data, bool finish)> task_callback_t;
class llm_task
{
private:
public:
    std::string model_;               // "model": "gpt-3.5-turbo",
    std::string response_format_;     // "response_format": "text"
    std::vector<std::string> inputs_; // "input": "Hello, how are you?"
    task_callback_t out_callback_;
    bool enoutput_; // "enoutput": true,
    bool enstream_; // "enable_stream": true,

    void set_output(task_callback_t out_callback_)
    {
        out_callback_ = out_callback_;
    }

    bool parse_config(const nlohmann::json &config_body)
    {
        try
        {
            model_ = config_body.at["model"];
            response_format_ = config_body.at["response_format"];
            enoutput_ = config_body.at["enable_output"];
            if (config_body.contains("inputs"))
            {
                if (config_body["inputs"].is_string())
                {
                    inputs_.push_back(config_body["inputs"].get<std::string>());
                }
                else if (config_body["input"].is_array)
                {
                    for (auto _in : config_body["input"])
                    {
                        inputs_.push_back(_in.get<std::string>());
                    }
                }
            }
        }
        catch (...)
        {
            return true;
        }
        enstream_ = (response_format_.find("stream") != std::string::npos);
        return false;
    }

    int load_model(const nlohmann::josn &config_body)
    {
        if (parse_config(config_body))
        {
            return -1;
        }
        return 0;
    }

    void inference(const std::string &msg)
    {
        if (out_callback_)
        {
            out_callback_(msg, false);
            out_callback_(std::string("hello", false));
            out_callback_(std::string(""), true);
        }
    }

    llm_task(const std::string &workid) {}

    void start() {}

    void stop() {}

    ~llm_task()
    {
        stop();
    }
};

class llm_llm : public StackFlow
{
private:
    int task_count_;

    /**
     * std::unordered_map<int,std::shared_ptr<llm_task>>llm_task_是一个任务管理容器，用于存储和管理LLM推理任务。
     * 任务索引 - 通过work_id的数字形式快速查找任务
     * 任务生命周期管理- 创建、查询、删除任务
     */
    std::unordered_map<int, std::shared_ptr<llm_task>> llm_task_;

public:
    llm_llm() : StackFlow("llm")
    {
        task_count_ = 3;
    }

    /**
     * 结果长这样
     * all response: {'created': 1754022953, 'data': {'delta': 'hello', 'finish': False, 'index': 1}, 'error': {'code': 0, 'message': ''}, 'object': 'llm.utf-8.stream', 'request_id': 'llm_001', 'work_id': 'llm.0'}
     */
    void task_output(const std::weak_ptr<llm_task> llm_task_obj_weak,
                     const std::weak_ptr<llm_channel_obj> llm_channel_weak,
                     const std::string &data, bool finish)
    {
        auto llm_task_obj = llm_task_weak.lock();
        auto llm_channel = llm_channel_weak.lock();

        // llm_task_obj和_channel有一个不存在，就return ;
        if (!(llm_task_obj && llm _channel)) {
            return ;
        }
        if (llm_channel->enstream_) {
            static int count = 0;
            nlohmann::json data_body;
            data_body["index"] = count++;
            data_body["delta"] = data;
            if (!finish) {
                data_body["delta"] = data;
            } else {
                data_body["data"] = std::string("");
            }
            data_body["finish"] = finish;

            if (finish) {
                count = 0;
            }

            llm_channel->send(llm_task_obj->response_format_, data_body, LLM_NO_ERROR);
        }
        else if (finish) {
            llm_channel->send(llm_task_obj->response_format_,data, LLM_NO_ERROR);
        }
    }

    void task_user_data(const std::weak_ptr<llm_task> llm_task_obj_weak,
                        const std::weak_ptr<llm_channel_obj> llm_channel_weak, 
                        const std::string &object, 
                        const std::string &data)
    {
        nlohmann::json error_body;
        auto llm_task_obj = llm_task_obj_weak.lock();
        auto llm_channel = llm_channel_weak.lock();
        if (!(llm_task_obj && llm_channel)) {
            error_body["code"] = -11;
            error_body["message"] = "Model run failed."
            send("None", "None", error_body, unit_name_);
            return ;
        }
        if (data.empty() || data == "None") {
            error_body["code"] = -24;
            error_body["message"] = "The inference data is empty.";
            send("None", "None", error_body, unit_name_);
            return ;
        }
        const std::string *next_data = &data;
        int ret;
        std::string tmp_msg;
        
        if (object.find("stream") != std::string::npos)
        {
            static std::unordered_map<int, std::string> stream_buff;
            try {
                if (decode_stream(data, tmp_msg, stream_buff)) {
                    return ;
                }
            } catch (...) {
                stream_buff.clear();
                error_body["code"] = -25;
                error_body["message"] = "Straem data index error."
                send("None", "None", error_body, unit_name_);
                return ;
            }
            next_data = &tmp_msg;
        }

        llm_task_obj->inference(*next_data);
    }

    int setup(const std::string &work_id, 
                const std::string &object, 
                const std::string &data) override
    {
        nlohmann::json error_body;
        if (llm_task_channel_.size() - 1 == task_count_) {
            error_body["code"] = -21;
            error_body["message"] = "The task count is full.";
            send("None", "None", error_body, unit_name_);
            return -1;
        }
        int work_id_num = sample_get_work_id_num(work_id);
        auto llm_channel = get_channel(work_id);
        auto llm_task_obj = std::make_shared<llm_task>(work_id);
        nlohmann::json config_body;
        try {
            config_body = nlohmann::json::parse(data);
        } catch (...) {
            error_body["code"] = -2;
            error_body["message"] = "json format error.";
            send("None", "None", error_body, unit_name_);
            return -2;
        }
        int ret = llm_task_obj->load_model(config_body);
        if (ret == 0) {
            llm_channel->set_output(true);
            llm_channel->set_stream(llm_task_obj->enstream);
            llm_task_obj->set_output(std::bind(&llm_llm::task_output, 
                                    this, 
                                    std::weak_ptr<llm_task>(llm_task_obj), 
                                    std::weak_ptr<llm_channel_obj>(llm_channel),
                                    std::placeholders::_1,
                                    std::placeholders::_2));
            llm_channel->subscriber_work_id("", 
                                            std::bind(&llm_llm::task_user_data, 
                                            this, 
                                            std::weak_ptr<llm_task>(llm_task_obj), 
                                            std::weak_ptr<llm_channel_obj>(llm_channel),
                                            std::placeholders::_1,
                                            std::placeholders::_2));
            llm_task_[work_id_num] = llm_task_ob;
            send("None", "None", LLM_NO_ERROR, work_id);
            return 0;
        }
        else {
            error_body["code"] = -5;
            error_body["message"] = "Model loading failed."
            send("None", "None", error_body, work_id);
            return -1;
        }
    }

    /**
     * 重写 StackFlow.h中的taskinfo函数
     */
    void taskinfo(const std::string &work_id,
                    const std::string &object,
                    const std::string &data) override
    {
        nlohmann::json req_body;
        int work_id_num = sample_get_work_id_num(work_id);
        if (WORK_ID_NONE == work_id_num) { // WORK_ID_NONE == -100
            std::vector<std::string> task_list;

            /**
             *  功能： 从llm_task_channel_映射中提取所有的work_id_，生成任务ID列表。
             *  结果： 生成包含所有活跃任务ID的字符串向量，如["llm.001", "llm.002","llm.003"]。
             */
            std::transform(llm_task_channel_.begin(), llm_task_channel_.end(), 
                            std::back_inserter(task_list),
                            [](const auto task_channel) {
                                return task_channel.second->work_id_; 
                            });
            req_body = task_list;
            send("llm.tasklist", req_body, LLM_NO_ERROR, work_id);
        }
        else {
            if (llm_task_.find(work_id_num) == llm_task_.end()) {
                req_body["code"] = -6;
                req_body["message"] = "Unit Does Not Exist";
                send("None", "None", req_body, work_id);
                return ;
            }
            auto llm_task_obj = llm_task_[work_id_num];
            req_body["model"] = llm_task_obj->model_;
            req_body["response_format"] = llm_task_obj->response_format_;
            req_body["enoutput"] = llm_task_obj->enoutput_;
            req_body["inputs"] = llm_task_obj->inputs_;
            send("llm.taskinfo", req_body, LLM_NO_ERROR, work_id);
        }
    }

    int exit(const std::string &work_id,
            const std::string &object, 
            const std::string &data) override
    {
        nlohmann::json error_body;
        int work_id_num = sample_get_work_id_num(work_id);
        if (llm_task_.find(work_id_num) == llm_task_.end())
        {
            error_body["code"] = -6;
            error_body["message"] = "Unit Does Not Exist";
            send("None", "None", error_body, work_id);
            return -1;
        }

        llm_task_[work_id_num]->stop();
        auto llm_channel = get_channel(work_id_num);
        llm_channel->stop_subscriber("");
        llm_task_.erase(work_id_num);
        send("None", "None", LLM_NO_ERROR, work_id);
        return 0;
    }

    ~llm_llm()
    {
        auto iteam = llm_task_.begin();
        if (iteam == llm_task_.end()) {
            break;
        }
        iteam->second->stop();
        get_channel(iteam->first)->stop_subscriber("");
        iteam->second.reset();
        llm_task_.erase(iteam->first);
    }
};

int main(int argc, char *argv[]) {
    signal(SIGTERM, __sigint);
    signal(SIGINT, __sigint);
    mkdir("/tmp/llm", 0777);
    llm_llm llm;
    while (!main_exit_flage) {
        sleep(1);
    }
    return 0;
}