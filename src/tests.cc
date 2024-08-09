#include "executor.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

TEST(ExecutorTest, PostTask){
    std::atomic<int32_t> x = 0;
    auto task = [&x](){
        x = 1;
    };
    {
        executor::Executor executor_;
        auto runner_tag = executor_.AddTaskRunner("Test");
        executor_.PostTask(runner_tag, std::move(task));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    EXPECT_EQ(x, 1);
}

TEST(ExecutorTest, PostTaskWithRet){
    int x = 0;
    {
        executor::Executor executor_;
        auto runner_tag = executor_.AddTaskRunner("Test");
        auto fut_ptr = executor_.PostTaskAndGetResult(runner_tag,[&x]()->int {x = 1; return 20;});

        ASSERT_TRUE(fut_ptr != nullptr);
        int ret = fut_ptr->get();
        EXPECT_EQ(ret,20);

    }

    EXPECT_EQ(x,1);
}

TEST(ExecutorTest, PostDelayedTask){
    int x = 0;
    auto task = [&x](){x = 1;};
    {
        executor::Executor executor_;   //先创建一个调度器,添加runner
        auto runner_tag = executor_.AddTaskRunner("Test");
        executor_.PostDelayedTask(runner_tag,std::move(task),std::chrono::milliseconds(500));

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        EXPECT_EQ(x,0);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    EXPECT_EQ(x,1);
}

TEST(ExecutorTest, PostRepeatedTask){
    int x = 0;
    auto task = [&x](){x += 1;};
    {
        executor::Executor executor_;
        auto runner_tag = executor_.AddTaskRunner("Test");
        executor_.PostRepeatedTask(runner_tag,std::move(task),std::chrono::milliseconds(500),5);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        EXPECT_EQ(x,1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        EXPECT_GE (x,2);
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
    EXPECT_EQ(x,5);
}

TEST(ExecutorTest, CancelRepeatedTask){
    int x = 0;
    auto task = [&x](){ x+= 1;};
    {
        executor::Executor executor_;
        auto runner_tag = executor_.AddTaskRunner("Test");
        auto task_id = executor_.PostRepeatedTask(runner_tag,std::move(task),std::chrono::milliseconds(500),5);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        EXPECT_EQ(x,1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        EXPECT_GE(x,2);
        executor_.CancelRepeatedTask(task_id); //这里取消了线程
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }

    EXPECT_EQ(x,3);
}

int main(int argc, char** argv){
    ::testing::InitGoogleTest(&argc,argv);
    return RUN_ALL_TESTS();
}