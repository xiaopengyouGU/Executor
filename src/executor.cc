#include "executor.h"

#include<functional>

namespace executor{

    Executor::Executor(){
        executor_context_ = std::make_unique<ExecutorContext>();
        executor_timer_ = std::make_unique<ExecutorTimer>();
    }

    Executor::~Executor(){
        executor_context_.reset();
        //the timer object needs to be destroyed after the context
        executor_timer_.reset();
    }

    TaskRunnerTag Executor::AddTaskRunner(const TaskRunnerTag & tag){
        DCHECK(!tag.empty());   //创建一个runner
        DCHECK(executor_context_);
        return executor_context_->AddTaskRunner(tag);   //具体操作由ExecutorContext实现
    }

    void Executor::PostTask(const TaskRunnerTag& runner_tag, Task task){
        DCHECK(!runner_tag.empty());
        DCHECK(executor_context_);
        //同一类runner_tag的task放在一起
        ExecutorContext::TaskRunner* task_runner = executor_context_->GetTaskRunner(runner_tag);
        DCHECK(!task_runner);
        task_runner->RunTask(std::move(task));
    }

    TaskRunnerTag Executor::ExecutorContext::AddTaskRunner(
        const TaskRunnerTag& tag
    ){
        std::lock_guard<std::mutex> lock(mutex_);   //上锁
        std::string latest_tag = tag;
        while(latest_tag.empty() ||
                task_runner_dict_.find(latest_tag)!= task_runner_dict_.end()){
                    latest_tag = GetNextRunnerTag();
                }
        TaskRunnerPtr runner = std::make_unique<executor::detail::ThreadPool>(latest_tag,1);
        runner->Start();
        task_runner_dict_.emplace(latest_tag,std::move(runner));    //创建一个新的线程
        return latest_tag;
    }

    TaskRunnerTag Executor::ExecutorContext::GetNextRunnerTag(){
        static uint64_t index = 0;
        ++index;
        return std::to_string(index);
    }

    Executor::ExecutorContext::TaskRunner* Executor::ExecutorContext::GetTaskRunner(
        const TaskRunnerTag&tag
    ){
        std::lock_guard<std::mutex> lock(mutex_);
        if(task_runner_dict_.find(tag) == task_runner_dict_.end())
        return nullptr;
        return task_runner_dict_[tag].get();
    }

    //Timer的具体实现
    Executor::ExecutorTimer::ExecutorTimer(){
        thread_pool_ = std::make_unique<executor::detail::ThreadPool>("ExecutorTimer",1);
        repeated_task_id_.store(0);
        running_.store(false);
    }

    Executor::ExecutorTimer::~ExecutorTimer(){
        Stop();
    }

    bool Executor::ExecutorTimer::Start(){
        if(running_)
        return true;

        running_.store(true);
        bool ret = thread_pool_->Start(); //启动,添加线程
        thread_pool_->RunTask(&Executor::ExecutorTimer::Run_,this);
        return ret; //在线程池中加了一个Run_，这个Run_会一直进行，直到程序析构
    }

    void Executor::ExecutorTimer::Stop(){
        running_.store(false);
        cond_.notify_all();
        thread_pool_.reset();
    }

    void Executor::ExecutorTimer::Run_(){ //按时间进行调度
        while(running_.load()){
            std::unique_lock<std::mutex> lock(mutex_);
            if(queue_.empty()){
                cond_.wait(lock);
                continue;
            }
            auto s = queue_.top();
            auto diff = s.time_point - std::chrono::high_resolution_clock::now();
            if(std::chrono::duration_cast<std::chrono::microseconds>(diff).count() > 0){
                cond_.wait_for(lock,diff);
                continue;
            }else{
                queue_.pop();
                lock.unlock();
                s.task();
            }

        }
    }

    void Executor::ExecutorTimer::PostDelayedTask(Task task,
                        const std::chrono::microseconds&delta){
        Internals s;
        s.time_point = std::chrono::high_resolution_clock::now()+delta;
        s.task = std::move(task);
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push(s);
            cond_.notify_all();
        }
    }

    RepeatedTaskId Executor::ExecutorTimer::PostRepeatedTask(
            Task task,
            const std::chrono::microseconds& delta,
            uint64_t repeat_num
    ){
        RepeatedTaskId id = GetNextRepeatedTaskId();
        repeated_id_state_set.insert(id);
        PostRepeatedTask_(std::move(task),delta,id,repeat_num); //让该函数进行具体操作
        return id;
    }

    void Executor::ExecutorTimer::CancelRepeatedTask(RepeatedTaskId task_id){
        repeated_id_state_set.erase(task_id);
    }

    void Executor::ExecutorTimer::PostTask_(
        Task task,
        const std::chrono::microseconds &delta,
        RepeatedTaskId repeated_task_id,
        uint64_t repeat_num
    ){
        PostRepeatedTask_(std::move(task),delta,repeated_task_id,repeat_num);
    }

    void Executor::ExecutorTimer::PostRepeatedTask_(
        Task task,
        const std::chrono::microseconds&delta,
        RepeatedTaskId repeated_task_id,
        uint64_t repeat_num
    ){
        if(repeated_id_state_set.find(repeated_task_id) == repeated_id_state_set.end() || 
        repeat_num == 0){ //查找该任务
            return;
        }

        task(); //执行一次task

        Task func = std::bind(&Executor::ExecutorTimer::PostTask_,this, std::move(task),
            delta,repeated_task_id, repeat_num - 1);
        
        Internals s;
        s.time_point = std::chrono::high_resolution_clock::now() + delta;
        s.repeated_id = repeated_task_id;
        s.task = std::move(func);       //这一步很关键这里func可以被move
    
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push(s);
            lock.unlock();  //解锁
            cond_.notify_all();
        }
    }

}