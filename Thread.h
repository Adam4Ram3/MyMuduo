#pragma once
#include "noncopyable.h"

#include <functional> // std::function
#include <thread>     // std::thread
#include <memory>     // std::shared_ptr
#include <unistd.h>   // pid_t
#include <string>     // std::string
#include <atomic>     // std::atomic

// Thread 类：对 std::thread 的一层封装，提供线程创建、命名、获取 tid 等功能
// 设计目标：
// 1. 保存线程入口函数（ThreadFunc）
// 2. 在线程真正启动后记录 Linux TID（轻量级进程号，区别于 pthread_t）
// 3. 支持线程命名，便于调试
// 4. 统计已创建线程数量（静态原子计数器）
// 5. 禁止拷贝（继承 noncopyable）
class Thread : noncopyable
{
public:
    using ThreadFunc = std::function<void()>; // 线程执行的回调类型，无参无返回

    explicit Thread(ThreadFunc func, const std::string &name = std::string());
    // 构造函数：保存回调与可选的线程名（若为空稍后自动生成）
    ~Thread(); // 析构：若线程已启动但未 join，通常需要分离或处理（具体实现里处理）

    void start(); // 启动线程：内部创建 std::thread，并在子线程里运行 func_
    void join();  // 等待线程结束：调用底层 thread_->join()

    bool started() const { return started_; }         // 查询线程是否已调用 start()
    pid_t tid() const { return tid_; }                // 返回 Linux 内核级线程 id（在子线程启动后获取）
    const std::string &name() const { return name_; } // 返回线程名

    static int numCreated() { return numCreated_; } // 返回已创建的线程总数（统计用途）

private:
    void setDefaultName(); // 若用户未指定 name，则生成一个默认名字（如 Thread1 / Thread2）

    bool started_;                        // 是否已调用 start()
    bool joined_;                         // 是否已调用 join()
    std::shared_ptr<std::thread> thread_; // 持有底层 std::thread 对象
    pid_t tid_;                           // 真实的 Linux 线程 TID（区别于 pthread_t）
    ThreadFunc func_;                     // 线程入口函数
    std::string name_;                    // 线程名
    static std::atomic_int numCreated_;   // 全局已创建线程计数器（用于生成默认名和统计）
};