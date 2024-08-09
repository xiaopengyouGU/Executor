#ifndef __EXECUTOR_EXECUTOR_H__
#define __EXECUTOR_EXECUTOR_H__
#include "thread_pool.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>

namespace executor{
    using Task = std::function<void(void)>;
    using TaskRunnerTag = std::string;
    using RepeatedTaskId = uint64_t;

    class Executor{
        //Timer是定时器,负责处理重复任务和延时任务
        //Context负责信息处理
        private:
        class ExecutorTimer{
            public:
                struct Internals{
                    std::chrono::time_point<std::chrono::high_resolution_clock> time_point;
                    Task task;  //一个可调用对象
                    RepeatedTaskId repeated_id;
                    //使用大堆序
                    bool operator<(const Internals& b)const{
                        return time_point > b.time_point;
                    }
                };
            public:
                ExecutorTimer();
                ~ExecutorTimer();

                ExecutorTimer(const ExecutorTimer& other) = delete;
                ExecutorTimer& operator=(const ExecutorTimer&other) = delete;

                bool Start();
                void Stop();

                void PostDelayedTask(Task task, const std::chrono::microseconds& delta);

                RepeatedTaskId PostRepeatedTask(Task task,
                                                const std::chrono::microseconds& delta,
                                                uint64_t repeat_num);

                void CancelRepeatedTask(RepeatedTaskId task_id);

            private:
                void Run_();

                void PostRepeatedTask_(Task task,
                                        const std::chrono::microseconds& delta,
                                        RepeatedTaskId repeated_task_id,
                                        uint64_t repeat_num);

                void PostTask_(Task task,
                                const std::chrono::microseconds& delta,
                                RepeatedTaskId repeated_task_id,
                                uint64_t repeat_num);
                
                RepeatedTaskId GetNextRepeatedTaskId(){ return repeated_task_id_++;}
            
            private:
                std::priority_queue<Internals> queue_;
                std::mutex mutex_;
                std::condition_variable cond_;
                std::atomic<bool> running_;
                std::unique_ptr<executor::detail::ThreadPool> thread_pool_;

                std::atomic<RepeatedTaskId> repeated_task_id_;
                std::unordered_set<RepeatedTaskId> repeated_id_state_set;
        };

        class ExecutorContext{
            public:
                ExecutorContext() = default;
                ~ExecutorContext() = default;

                ExecutorContext(const ExecutorContext& other) = delete;
                ExecutorContext& operator=(const ExecutorContext& other) = delete;

                TaskRunnerTag AddTaskRunner(const TaskRunnerTag& tag);

            private:
                using TaskRunner = executor::detail::ThreadPool;
                using TaskRunnerPtr = std::unique_ptr<executor::detail::ThreadPool>;
                friend class Executor;      //使用友元

            private:
                TaskRunner* GetTaskRunner(const TaskRunnerTag& tag);

                TaskRunnerTag GetNextRunnerTag();

            private:
                std::unordered_map<TaskRunnerTag, TaskRunnerPtr> task_runner_dict_;
                std::mutex mutex_;
        };

        public:
            Executor();
            ~Executor();

            Executor(const Executor& other) = delete;
            Executor& operator=(const Executor& other) = delete;

            TaskRunnerTag AddTaskRunner(const TaskRunnerTag& tag);

            void PostTask(const TaskRunnerTag& runner_tag, Task task);

            template<typename R, typename P>
            void PostDelayedTask(const TaskRunnerTag& runner_tag,
                                        Task task,
                                        const std::chrono::duration<R,P>&delta){
               DCHECK(!runner_tag.empty());
               DCHECK(executor_context_);
               DCHECK(executor_timer_);
               Task func = std::bind(&Executor::PostTask,this,runner_tag,std::move(task));

               //It's guaranteed to start only once inside
               executor_timer_->Start();
               executor_timer_->PostDelayedTask(std::move(func),
               std::chrono::duration_cast<std::chrono::microseconds>(delta));                            
        }

        template<typename R, typename P>
        RepeatedTaskId PostRepeatedTask(const TaskRunnerTag& runner_tag,
                                        Task task,
                                        const std::chrono::duration<R,P>&delta,
                                        uint64_t repeat_num){
                DCHECK(!runner_tag.empty());
                DCHECK(executor_context_);
                DCHECK(executor_timer_);
                //处理成员函数
                Task func = std::bind(&Executor::PostTask,this,runner_tag,std::move(task));

               //It's guaranteed to start only once inside
               executor_timer_->Start();        

               return executor_timer_->PostRepeatedTask(
                    std::move(func),
                    std::chrono::duration_cast<std::chrono::microseconds>(delta),
                    repeat_num
               ) ;                    
        }

        void CancelRepeatedTask(RepeatedTaskId task_id){
            DCHECK(executor_timer_);
            executor_timer_->CancelRepeatedTask(task_id);
        }

        template <typename F, typename ... Args>
        auto PostTaskAndGetResult(const TaskRunnerTag& runner_tag,
                                    F&&f,
                                    Args&&... args)->std::shared_ptr<std::future<std::result_of_t<F(Args...)>>>{
                DCHECK(!runner_tag.empty());
                DCHECK(executor_context_);
                DCHECK(executor_timer_);

                ExecutorContext::TaskRunner* task_runner = 
                executor_context_->GetTaskRunner(runner_tag);
                DCHECK(!task_runner);

                auto ret = 
                    task_runner->RunTask(std::forward<F>(f), std::forward<Args>...);
                return ret;
        }

        private:
            std::unique_ptr<ExecutorContext> executor_context_;
            std::unique_ptr<ExecutorTimer> executor_timer_;
    };
     
}

#endif