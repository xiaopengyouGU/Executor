#ifndef __THREAD_POOL_H
#define __THREAD_POOL_H
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#define DCHECK(...) (void)0;
//ThreadInfo类析构时开始执行线程（thread）
namespace executor{
    namespace detail{
        class ThreadPool{
            public:
            ThreadPool(const std::string& tag, uint32_t thread_count){
                this->tag_ = tag;
                this->is_available_.store(false);
                this->is_shutdown_.store(false);
                this->thread_count_ = thread_count;
            }
            
            ThreadPool(const ThreadPool& other) = delete;
            ThreadPool& operator=(const ThreadPool& otner) = delete;

            ~ThreadPool(){ Stop();} //

            bool Start();
            
            void Stop();

            template<typename F, typename ... Args>
            auto RunTask(F&& f, Args &&... args) -> std::shared_ptr<std::future<std::result_of_t<F(Args...)>>>{
                if(this->is_shutdown_.load() || !this->is_available_.load()){
                    return nullptr;
                }
                //这里F为可调用对象,std::result_of_t返回可调用对象的返回类型
                using return_type = std::result_of_t<F(Args...)>;
                auto task = std::make_shared<std::packaged_task<return_type()>>(
                    std::bind(std::forward<F>(f), std::forward<Args>(args)...));
                //利用了bind 把 f的参数绑定到 args,返回一个新的可调用对象
                //是函数适配器
                //std::forward保持F参数的左右值特性，称为完美转发
                std::future<return_type> res = task->get_future(); //获取task的结果
                {
                    std::lock_guard<std::mutex> lock(this->task_mutex_); //开启线程锁
                    this->tasks_.emplace([task](){(*task)();}); //添加任务
                }

                this->task_cv_.notify_all(); //唤醒所有等待的进程
                return std::make_shared<std::future<std::result_of_t<F(Args...)>>>(std::move(res));

            }

            private:
            void AddThread();

            private:
            using ThreadPtr = std::shared_ptr<std::thread>;
            using Task = std::function<void()>;

            struct ThreadInfo{
                ThreadInfo() = default;
                ~ThreadInfo();

                ThreadPtr ptr{nullptr};
            };


            using ThreadInfoPtr = std::shared_ptr<ThreadInfo>;

            private:
            std::vector<ThreadInfoPtr> worker_threads_;    //工作线程
            std::queue<Task> tasks_;    

            std::mutex task_mutex_;
            std::condition_variable task_cv_;

            std::string tag_;

            std::atomic<uint32_t> thread_count_;
            std::atomic<bool> is_shutdown_;
            std::atomic<bool> is_available_; 
        };
    }
}
#endif