#pragma once

#include "EventLoop.h"
#include "Acceptor.h"
#include "InetAddress.h"
#include "noncopyable.h"
#include "EventLoopThreadPool.h"
#include "Callbacks.h" // 引入上面定义的回调类型

#include <functional>
#include <string>
#include <atomic>
#include <memory>
#include <unordered_map>

/**
 * @brief 对外使用的服务器主类 TcpServer。
 * @details
 * 它的核心职责是管理 Acceptor 和 EventLoopThreadPool, 
 * 并处理新连接的到来, 将新连接分发给一个合适的 subLoop。
 * 用户通过设置各种回调函数来注入自己的业务逻辑。
 */
class TcpServer : noncopyable
{
public:
    /// @brief I/O 线程初始化回调函数类型
    using ThreadInitCallback = std::function<void(EventLoop *)>;
    
    /// @brief 用于控制是否开启 SO_REUSEPORT 的选项
    enum Option
    {
        kNoReusePort,
        kReusePort,
    };

    /**
     * @brief TcpServer 的构造函数。
     * @param loop 用户创建的 mainLoop (主Reactor), 负责接受新连接。
     * @param listenAddr 服务器监听的IP地址和端口。
     * @param nameArg 服务器的名称。
     * @param option 是否开启端口复用。
     */
    TcpServer(EventLoop *loop,
              const InetAddress &listenAddr,
              const std::string &nameArg,
              Option option = kNoReusePort);
    
    /**
     * @brief TcpServer 的析构函数。
     * @note 确保所有线程和连接都被安全地关闭和销毁。
     */
    ~TcpServer();

    /**
     * @brief 设置每个I/O线程启动时的初始化回调。
     * @param cb 用户提供的回调函数。
     */
    void setThreadInitCallback(const ThreadInitCallback &cb) { threadInitCallback_ = cb; }

    /**
     * @brief 设置连接建立和断开时的回调。
     * @param cb 用户提供的业务逻辑。
     */
    void setConnectionCallback(const ConnectionCallback &cb) { connectionCallback_ = cb; }

    /**
     * @brief 设置消息到来的回调(有数据可读)。
     * @param cb 用户提供的业务逻辑。
     */
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }

    /**
     * @brief 设置消息发送完成后的回调。
     * @param cb 用户提供的业务逻辑。
     */
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }

    /**
     * @brief 设置底层 I/O 线程 (subLoop) 的数量。
     * @param numThreads 线程数量。如果为0, 所有操作都在 mainLoop 中进行 (单线程模式)。
     */
    void setThreadNum(int numThreads);

    /**
     * @brief 开启服务器监听。
     * @note 此函数必须在 loop() 之前被调用。
     */
    void start();

private:
    /**
     * @brief Acceptor 接受一个新连接后, 会调用这个函数。
     * @param sockfd 新连接的socket文件描述符。
     * @param peerAddr 客户端的地址信息。
     * @note 这个函数运行在 mainLoop 所在的线程中。
     */
    void newConnection(int sockfd, const InetAddress &peerAddr);

    /**
     * @brief 当一个 TcpConnection 关闭时, 会调用这个函数来从服务器中移除该连接。
     * @param conn 即将移除的 TcpConnection 的智能指针。
     * @note 这个函数可能由任意一个 subLoop 线程调用, 因此需要保证线程安全。
     */
    void removeConnection(const TcpConnectionPtr &conn);

    /**
     * @brief removeConnection 的实际执行函数, 保证在 mainLoop 线程中执行。
     * @param conn 即将移除的 TcpConnection 的智能指针。
     */
    void removeConnectionInLoop(const TcpConnectionPtr &conn);
    
    /// @brief 用于存储所有活动连接的 map, key 是连接的名称, value 是连接的智能指针。
    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    /// @brief 用户传入的 mainLoop, 即主 Reactor。
    EventLoop *loop_;
    /// @brief 服务器的 IP:Port 字符串。
    const std::string ipPort_;
    /// @brief 服务器的名称。
    const std::string name_;
    /// @brief Acceptor 对象, 用于接受新连接。其生命周期由 unique_ptr 管理。
    std::unique_ptr<Acceptor> acceptor_;
    /// @brief I/O 线程池。其生命周期由 shared_ptr 管理。
    std::shared_ptr<EventLoopThreadPool> threadPool_;

    /// @brief 用户设置的连接回调函数。
    ConnectionCallback connectionCallback_;
    /// @brief 用户设置的消息回调函数。
    MessageCallback messageCallback_;
    /// @brief 用户设置的写完成回调函数。
    WriteCompleteCallback writeCompleteCallback_;

    /// @brief 用户设置的线程初始化回调函数。
    ThreadInitCallback threadInitCallback_;
    /// @brief 原子整型, 标记服务器是否已启动。防止 start() 被多次调用。
    std::atomic_int started_;

    /// @brief 用于为新连接生成唯一名称的计数器。
    int nextConnId_;
    /// @brief 存储所有活动连接的 map 实例。
    ConnectionMap connections_;
};