#pragma once

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"

#include <memory>
#include <string>
#include <atomic>

class Channel;
class EventLoop;
class Socket;

/**
 * @brief TcpConnection 是对一个已建立的TCP连接的封装。
 * @details
 * 它是服务器与单个客户端之间通信的桥梁。每个 TcpConnection 对象都唯一地对应一个
 * 客户端连接, 并管理着该连接的完整生命周期。
 * 它负责数据的收发, 并将收到的数据(通过MessageCallback)和连接状态的变化
 * (通过ConnectionCallback)通知给上层业务逻辑。
 * 这个类的对象生命周期由 std::shared_ptr 管理, 确保了在异步回调中的安全性。
 */
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    /// @brief 连接状态的枚举
    enum StateE
    {
        kDisconnected, // 已断开连接
        kConnecting,   // 正在连接 (初始状态)
        kConnected,    // 已连接
        kDisconnecting // 正在断开连接
    };

    /**
     * @brief 构造函数。
     * @param loop TcpConnection 所属的 EventLoop (一个 subLoop)。
     * @param name 连接的名称。
     * @param sockfd Acceptor 接受的新连接的 socket fd。
     * @param localAddr 服务器本地的地址信息。
     * @param peerAddr 客户端对端的地址信息。
     */
    TcpConnection(EventLoop *loop,
                  const std::string &name, // 建议使用 const std::string&
                  int sockfd,
                  const InetAddress &localAddr,
                  const InetAddress &peerAddr);

    /**
     * @brief 析构函数。
     * @note 会打印日志, 方便追踪连接的销毁。
     */
    ~TcpConnection();

    // --- 提供给外部查询状态的接口 ---
    EventLoop *getLoop() const { return loop_; }
    const std::string &name() const { return name_; }
    const InetAddress &localAddress() const { return localAddr_; }
    const InetAddress &peerAddress() const { return peerAddr_; }
    bool connected() const { return state_ == kConnected; }

    // --- 提供给外部进行数据收发和连接管理的接口 ---

    // /**
    //  * @brief 发送数据。
    //  * @param message 要发送的数据的 C 风格字符串指针。
    //  * @param len 数据的长度。
    //  * @note 这是一个线程安全的操作, 可以由非IO线程调用。
    //  */
    // void send(const void *message, size_t len);
    void send(const std::string &buf); // 保留这个方便的接口
    // (可选，但推荐) 增加一个右值引用版本，提高效率
    void send(std::string &&buf);

    /**
     * @brief 关闭连接 (优雅关闭)。
     * @details
     * 主动关闭方会调用 shutdownWrite() 关闭写半部分,
     * 等待对端关闭连接后, 再由 handleClose() 完成最终的清理。
     * @note 这是一个线程安全的操作。
     */
    void shutdown();

    // --- 用户回调函数的设置接口 ---
    void setConnectionCallback(const ConnectionCallback &cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }
    void setHighWaterMarkCallback(const HighWaterMarkCallback &cb, size_t highWaterMark)
    {
        highWaterMarkCallback_ = cb;
        highWaterMark_ = highWaterMark;
    }

    /**
     * @brief 设置关闭回调。
     * @details 这是由 TcpServer 使用的内部回调, 用于在连接关闭时通知 TcpServer 移除自己。
     */
    void setCloseCallback(const CloseCallback &cb) { closeCallback_ = cb; }

    // --- 框架内部使用的生命周期管理函数 ---

    /**
     * @brief 当 TcpServer 创建一个新的 TcpConnection 对象后, 会调用此函数。
     * @details
     * 这个函数是连接建立的“收尾”工作, 它会将 Channel 注册到 Poller,
     * 并执行用户设置的 ConnectionCallback。
     * @note 这个函数必须在所属的 EventLoop 线程中被调用。
     */
    void connectEstablished();

    /**
     * @brief 当 TcpServer 移除一个 TcpConnection 对象前, 会调用此函数。
     * @note 这个函数必须在所属的 EventLoop 线程中被调用。
     */
    void connectDestroyed();

private:
    /**
     * @brief Channel 的读事件回调, 由 EventLoop::loop() 调用。
     * @param receiveTime Poller返回的事件发生时间戳。
     */
    void handleRead(Timestamp receiveTime);

    /**
     * @brief Channel 的写事件回调, 由 EventLoop::loop() 调用。
     */
    void handleWrite();

    /**
     * @brief Channel 的关闭事件回调, 由 EventLoop::loop() 调用。
     */
    void handleClose();

    /**
     * @brief Channel 的错误事件回调, 由 EventLoop::loop() 调用。
     */
    void handleError();

    /**
     * @brief send() 的线程安全实现。它将实际的发送操作派发到IO线程执行。
     */
    void sendInLoop(const void *message, size_t len);
    void sendInLoop(const std::string &message); // 增加一个string的重载

    /**
     * @brief shutdown() 的线程安全实现。它将实际的关闭操作派发到IO线程执行。
     */
    void shutdownInLoop();

    /**
     * @brief 设置当前连接的内部状态。
     * @details 这是 TcpConnection 内部驱动状态机使用的辅助函数，
     * 用于在合适的时机（如连接建立、开始关闭、关闭完成）更新 state_。
     * @param state 新的状态枚举值（kConnecting / kConnected / kDisconnecting / kDisconnected）。
     * @note 仅应在所属 EventLoop 线程中、且由框架内部代码调用；业务层不要直接修改状态，
     *       应通过 send() / shutdown() 等高层接口触发状态变化，保持状态机一致性。
     */
    void setState(StateE state) { state_ = state; }

    /// @brief 所属的 EventLoop (subLoop)。
    EventLoop *loop_;
    /// @brief 连接的唯一名称。
    const std::string name_;
    /// @brief 连接的状态, 使用原子类型保证多线程下的可见性。
    std::atomic<StateE> state_;
    // /// @brief 标识是否正在读取数据, 用于控制Channel的enableReading。
    // bool reading_;

    /// @brief 底层的 socket 封装, unique_ptr 保证了资源的独占性和自动释放。
    std::unique_ptr<Socket> socket_;
    /// @brief 底层的 Channel 封装, unique_ptr 保证了资源的独占性和自动释放。
    std::unique_ptr<Channel> channel_;

    /// @brief 服务器本地的地址。
    const InetAddress localAddr_;
    /// @brief 客户端对端的地址。
    const InetAddress peerAddr_;

    // --- 用户回调 ---
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    HighWaterMarkCallback highWaterMarkCallback_;

    // --- 内部回调 ---
    /// @brief 用于通知 TcpServer 移除此连接的回调。
    CloseCallback closeCallback_;

    /// @brief 高水位标记, 用于流量控制。
    size_t highWaterMark_;

    /// @brief 输入(接收)缓冲区。
    Buffer inputBuffer_;
    /// @brief 输出(发送)缓冲区。
    Buffer outputBuffer_;
};