#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

// 前置声明, 避免在头文件中引入完整的类定义, 降低耦合度
class Channel;
class Poller;

/**
 * @brief EventLoop 是事件循环的核心类, 它是 Reactor 模式的“反应堆”。
 * * EventLoop 采用了 "one loop per thread" 的思想, 即每个I/O线程都有且仅有一个 EventLoop 对象。
 * 它的核心职责是驱动整个事件循环, 拥有并管理 Poller 和 Channels, 
 * 并负责处理跨线程的任务调用。
 */
class EventLoop : noncopyable
{
public:
    /**
     * @brief 定义了一个通用的函数对象类型 Functor, 用于跨线程任务的回调。
     */
    using Functor = std::function<void()>;
    
    /**
     * @brief 构造函数。
     * @note 会记录当前线程ID, 并初始化Poller和用于线程唤醒的eventfd。
     */
    EventLoop();

    /**
     * @brief 析构函数。
     * @note 会确保EventLoop对象的销毁在其所属的线程中进行。
     */
    ~EventLoop();

    /**
     * @brief 开启事件循环。
     * 这是EventLoop的核心驱动函数, 调用后线程会进入阻塞状态等待事件发生。
     * @note 此函数必须在创建EventLoop对象的那个线程中调用 (IO线程)。
     */
    void loop();

    /**
     * @brief 退出事件循环。
     * @note 这是一个线程安全的操作, 可以由其他线程调用来请求退出事件循环。
     * 退出不是立即的, 而是在下一次循环迭代中安全退出。
     */
    void quit();
    
    /**
     * @brief 获取Poller返回事件时的时间戳。
     * @return Timestamp Poller返回的时间戳。
     */
    Timestamp pollReturnTime() const { return pollReturnTime_; }

    /**
     * @brief 在当前EventLoop的线程中执行一个任务。
     * * 如果调用此函数的线程就是EventLoop所属的IO线程, 则同步立即执行该任务。
     * 如果是其他线程调用的, 则将任务放入任务队列, 并唤醒IO线程来异步执行。
     * * @param cb 需要执行的回调任务。
     * @note 提供了线程安全的任务执行机制。
     */
    void runInLoop(Functor cb);

    /**
     * @brief 将一个任务放入任务队列, 由IO线程在下一轮循环中执行。
     * * 与 runInLoop 不同, 此函数总是将任务放入队列, 而不会立即执行。
     * * @param cb 需要放入队列的回调任务。
     * @note 保证了任务的异步执行, 适用于需要延迟执行或避免递归调用的场景。
     */
    void queueInLoop(Functor cb);

    /**
     * @brief 唤醒当前EventLoop所在的IO线程。
     * * 主要由其他线程在向任务队列放入新任务后调用, 用于唤醒可能阻塞在poll()的IO线程。
     * @note 其底层实现是向 wakeupFd_ 写入一个字节。
     */
    void wakeup();

    /**
     * @brief 在Poller中更新一个Channel。
     * @param channel 需要更新的Channel指针。
     * @note 这是一个线程安全的操作, 但通常应该在IO线程中调用。
     */
    void updateChannel(Channel* channel);

    /**
     * @brief 从Poller中移除一个Channel。
     * @param channel 需要移除的Channel指针。
     * @note 这是一个线程安全的操作, 但通常应该在IO线程中调用。
     */
    void removeChannel(Channel* channel);

    /**
     * @brief 检查Poller中是否已包含指定的Channel。
     * @param channel 需要检查的Channel指针。
     * @return bool 如果包含则返回true, 否则返回false。
     */
    bool hasChannel(Channel* channel);

    /**
     * @brief 判断当前代码执行的线程是否为该EventLoop实例所属的IO线程。
     * @return bool 如果是则返回true, 否则返回false。
     * @note 这是保证“one loop per thread”模型正确性的核心断言。
     */
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

private:
    /**
     * @brief 用于处理 wakeupFd_ 上的可读事件的回调函数。
     * @note 当其他线程通过wakeup()唤醒此IO线程时, 该函数被触发,
     * 其作用是简单地从wakeupFd_中读取数据, 以便让Channel重新变为非触发状态。
     */
    void handleRead();

    /**
     * @brief 执行任务队列中的所有待处理任务。
     * @note 在每次事件循环的末尾被调用, 确保跨线程任务得到及时处理。
     */
    void doPendingFunctors();

    /// @brief 定义了Channel指针的列表类型
    using ChannelList = std::vector<Channel *>;

    /// @brief 原子布尔值, 标识事件循环是否正在运行 (looping_为true)。
    std::atomic_bool looping_;
    /// @brief 原子布尔值, 标识事件循环是否需要退出。
    std::atomic_bool quit_;

    /// @brief 记录当前EventLoop对象所属线程的ID。
    const pid_t threadId_;
    /// @brief Poller返回事件时的时间戳。
    Timestamp pollReturnTime_;
    /// @brief EventLoop拥有的Poller子系统 (采用unique_ptr管理其生命周期)。
    std::unique_ptr<Poller> poller_;

    /// @brief 用于唤醒的eventfd。其他线程通过向此fd写入8字节数据来唤醒当前线程的poll阻塞。
    int wakeupFd_;
    /// @brief 封装了wakeupFd_的Channel, 专门用于监听来自其他线程的唤醒事件。
    std::unique_ptr<Channel> wakeupChannel_;

    /// @brief Poller返回的当前活跃的Channel列表。
    ChannelList activeChannels_;

    /// @brief 原子布尔值, 标识当前是否正在执行任务队列中的回调。用于防止在处理回调时再次向队列添加任务的递归情况。
    std::atomic_bool callingPendingFunctors_;
    /// @brief 存储了其他线程请求在此IO线程中执行的回调函数任务队列。
    std::vector<Functor> pendingFunctors_;
    /// @brief 用于保护pendingFunctors_任务队列的互斥锁, 保证其在多线程环境下的添加操作是安全的。
    std::mutex mutex_;
};