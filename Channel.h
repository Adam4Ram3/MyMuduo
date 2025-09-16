#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <functional>
#include <memory>

// 前置声明, 降低头文件耦合度, 避免循环引用
class EventLoop;

/**
 * @brief Channel 是事件通道, 它是网络库的核心组件之一。
 * @details
 * Channel 并不拥有文件描述符(fd), 它也不是一个网络连接的封装。
 * 它是一个高度抽象的类, 它的核心职责是:
 * 1. 封装一个文件描述符(sockfd)。
 * 2. 封装这个fd上我们“感兴趣”的I/O事件(events_), 如 EPOLLIN, EPOLLOUT。
 * 3. 封装当 Poller 报告这个fd上有事件发生时, 需要执行的回调函数(Callbacks)。
 *
 * 在 Reactor 模型中, Channel 扮演了事件“分发(Demultiplex)”的角色。
 * Poller 负责侦测哪些fd上有事件, EventLoop 负责轮询并调用 Channel 的事件处理函数,
 * 而 Channel 则根据具体发生的事件(revents_)调用相应的用户回调。
 * * 它的生命周期由其所有者(如 Acceptor, TcpConnection)管理。
 */
class Channel : noncopyable
{
public:
    /// @brief 定义了一个通用的、无参数的事件回调函数类型。
    using EventCallback = std::function<void()>;
    /// @brief 定义了专门用于读事件的回调函数类型, 它会接收事件发生的时间戳。
    using ReadEventCallback = std::function<void(Timestamp)>;

    /**
     * @brief 构造函数。
     * @param loop Channel 所属的 EventLoop。
     * @param fd Channel 负责的文件描述符。
     */
    Channel(EventLoop *loop, int fd);
    
    /**
     * @brief 析构函数。
     */
    ~Channel();

    /**
     * @brief 事件处理的总入口, 由 EventLoop::loop() 调用。
     * @details 当 Poller 报告此 Channel 有事件发生时, EventLoop 会调用此函数。
     * 它内部会根据 revents_ 的值, 分发到具体的 handleRead, handleWrite 等回调上。
     * @param receiveTime Poller 返回事件时的时间戳。
     */
    void handleEvent(Timestamp receiveTime);

    // --- 设置各种事件回调的接口 ---
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    /**
     * @brief 将 Channel 的生命周期与一个 shared_ptr "绑定"起来。
     * @details
     * 这是一个核心的、保证异步回调安全性的机制。
     * 它使用 weak_ptr 观察一个所有者对象(如 TcpConnection)。
     * 在 handleEvent 执行回调前, 会尝试将 weak_ptr 提升为 shared_ptr,
     * 如果提升成功, 说明所有者对象还存活, 可以安全调用回调;
     * 如果提升失败, 说明所有者对象已经销毁, 就不再调用回调, 避免了悬空指针错误。
     * @param obj 指向所有者对象的 shared_ptr。
     */
    void tie(const std::shared_ptr<void> &obj);

    // --- 查询状态的接口 ---
    int fd() const { return fd_; }
    int events() const { return events_; }
    
    /**
     * @brief 设置实际发生的事件。
     * @note 这个函数由 Poller 在 poll() 之后调用, 用于通知 Channel 内核报告了哪些事件。
     */
    void set_revents(int revt) { revents_ = revt; }

    // --- 控制监听事件类型的接口 ---

    /**
     * @brief 启用读事件监听。
     * @details 会修改 events_ 标志位, 并调用 update() 将变化同步到 Poller。
     */
    void enableReading()
    {
        events_ |= kReadEvent;
        update();
    }
    void disableReading()
    {
        events_ &= ~kReadEvent;
        update();
    }
    void enableWriting()
    {
        events_ |= kWriteEvent;
        update();
    }
    void disableWriting()
    {
        events_ &= ~kWriteEvent;
        update();
    }
    void disableAll()
    {
        events_ = kNoneEvent;
        update();
    }

    // --- 查询当前监听状态的接口 ---
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }
    bool isReading() const { return events_ & kReadEvent; }

    // --- 供 Poller 内部使用的接口 ---
    int index() { return index_; }
    /**
     * @brief 设置此 Channel 在 Poller 中的状态索引。
     * @details 这是 Poller 内部的一个优化, 用于快速定位 Channel 的状态(kNew, kAdded, kDeleted)。
     */
    void set_index(int idx) { index_ = idx; }

    // --- 供框架内部使用的接口 ---
    EventLoop *ownerLoop() { return loop_; }
    /**
     * @brief 请求 EventLoop 将此 Channel 从 Poller 中移除。
     */
    void remove();

private:
    /**
     * @brief 私有辅助函数, 当 events_ 发生变化时, 调用此函数来通知 EventLoop,
     * 由 EventLoop 再去调用 Poller::updateChannel 来将变化同步到内核。
     */
    void update();
    
    /**
     * @brief 带有生命周期安全检查的内部事件处理函数。
     * @details 在执行回调前, 会先检查 tie_ 是否有效。
     */
    void handleEventWithGuard(Timestamp receiveTime);

    // 内部定义的事件类型常量, 用于解耦 Poller 的具体实现(epoll/poll)
    static const int kNoneEvent;  // 0
    static const int kReadEvent;  // EPOLLIN | EPOLLPRI
    static const int kWriteEvent; // EPOLLOUT

    /// @brief 所属的 EventLoop, Channel 的所有操作都会在其中进行。
    EventLoop *loop_; 
    /// @brief Channel 负责的文件描述符, 但不拥有它。
    const int fd_;
    /// @brief 用户注册的、我们“感兴趣”的事件的位掩码。
    int events_;
    /// @brief Poller 返回的、在fd上“实际发生”的事件的位掩码。
    int revents_;
    /// @brief 由 Poller 使用, 标记此 Channel 在 Poller 中的状态(kNew, kAdded, kDeleted)。
    int index_;

    /// @brief 用于实现生命周期绑定的 weak_ptr, 观察其所有者对象。
    std::weak_ptr<void> tie_;
    /// @brief 标记是否启用了 tie_ 机制。
    bool tied_;

    // --- 事件回调函数成员 ---
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};