#include "thread_pool.h"

namespace executor{
    namespace detail{
        ThreadPool::ThreadInfo::~ThreadInfo(){
            if(ptr && ptr->joinable()){
                ptr->join();
            }
        }

        void ThreadPool::AddThread(){
            ThreadInfoPtr thread_ptr = std::make_shared<ThreadInfo>();
            static uint64_t count = 0;
            ++count;
            auto func = [this](){
                uint64_t thread_id = count;
                for(;;){
                    Task task;
                    {
                        std::unique_lock<std::mutex> lock(this->task_mutex_);

                        this->task_cv_.wait(lock,[this]{
                            return this->is_shutdown_ || (!this->tasks_.empty())
                        ;});

                        if(this->is_shutdown_){
                            break;
                    }

                        if(this->tasks_.empty()){
                            continue;
                        }

                        task = std::move(this->tasks_.front());
                        this->tasks_.pop();
                }
                task();     //执行task
            }
        };
            thread_ptr->ptr = std::make_shared<std::thread>(std::move(func));  
            this->worker_threads_.emplace_back(std::move(thread_ptr));  //工作线程中添加一个线程
        }

        void ThreadPool::Stop(){
            if(is_available_.load()){
                is_shutdown_.store(true);
                task_cv_.notify_all();
                is_available_.store(false);
            }

            this->worker_threads_.clear();      //清空工作线程
        }

        bool ThreadPool::Start(){
            if(this->is_available_.load()){
                return false;
            }
            this->is_available_.store(true);
            uint32_t thread_count = thread_count_.load();
            for(uint32_t i = 0; i < thread_count; i++){
                AddThread();
            }
            return true;
        }
    }
}