#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

#include <functional>
#include <errno.h>

// 辅助函数, 用于检查并确保传入的EventLoop指针有效, 防止后续的空指针解引用
static EventLoop *checkLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d TcpConnection Looop is null\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

/**
 * @brief TcpConnection 构造函数
 * @details
 * 由 TcpServer::newConnection 在主线程 (mainLoop) 中调用,
 * 用于创建一个新的连接对象。此时TCP三次握手已经完成, 连接已经建立。
 * 这个构造函数的核心任务是“组装”一个新的连接实例, 为其配备好Socket和Channel,
 * 并预设好当底层事件发生时, 应该由TcpConnection的哪个成员函数来处理。
 */
TcpConnection::TcpConnection(EventLoop *loop,
                             const std::string &nameArg,
                             int sockfd,
                             const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(checkLoopNotNull(loop)), // 必须属于一个subLoop
      name_(nameArg),
      state_(kConnecting),                 // 初始状态为“正在连接”
                                           //   reading_(true),
      socket_(new Socket(sockfd)),         // 封装已连接的sockfd
      channel_(new Channel(loop, sockfd)), // 为该sockfd创建一个专属的Channel
      localAddr_(localAddr),
      peerAddr_(peerAddr),
      highWaterMark_(64 * 1024 * 1024) // 默认高水位标记64M
{
    // 【核心回调绑定】
    // 将 Channel 上的底层事件回调, 精确地绑定到 TcpConnection 的成员函数上。
    // 这就像是为“项目经理”(Channel)设定好了行动预案:
    // - "如果客户有新消息(可读事件), 就执行我的 handleRead 方法"
    // - "如果可以向客户发送更多数据(可写事件), 就执行我的 handleWrite 方法"
    // - "如果连接线路被挂断, 就执行我的 handleClose 方法"
    // - "如果线路出错, 就执行我的 handleError 方法"
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this));

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d", name_.c_str(), sockfd);
    socket_->setKeepAlive(true); // 默认开启TCP保活机制
}

/**
 * @brief TcpConnection 析构函数
 * @details 打印日志以方便追踪连接的销毁时机。
 * 由于socket_和channel_都是unique_ptr, 它们的资源会在这里被自动释放,
 * 完美符合RAII的要求。
 */
TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d",
             name_.c_str(), channel_->fd(), (int)state_);
}

/**
 * @brief 【线程安全的公有接口】发送数据。
 * @details
 * 这是暴露给用户的最高层发送接口。它可以被任何线程(主线程、业务逻辑线程等)调用。
 * 为了保证线程安全, 它内部会将真正的发送操作派发到此连接所属的IO线程中执行。
 * @param message 要发送的数据的指针。
 * @param len 数据的长度。
 */
// void TcpConnection::send(const void *message, size_t len)
// {
//     if (state_ == kConnected) // 只有在连接状态下才能发送
//     {
//         if (loop_->isInLoopThread()) // 优化: 如果调用者恰好就是IO线程, 则直接执行
//         {
//             sendInLoop(message, len);
//         }
//         else // 如果是其他线程调用, 则需要将任务派发给IO线程
//         {
//             loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, message, len));
//         }
//     }
// }
// 公共接口: 接收 const 左值引用 (会发生拷贝)
void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            // 使用 lambda 捕获 buf 的副本并在 IO 线程中调用，不依赖于对重载成员函数指针的解析
            loop_->runInLoop([this, buf]() { this->sendInLoop(buf); });
        }
    }
}
// 公共接口: 接收 右值引用 (会发生移动，效率更高)
void TcpConnection::send(std::string &&buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            // 这里可以直接移动，进一步优化
            sendInLoop(std::move(buf));
        }
        else
        {
            // 使用 move-capturing lambda 将 rvalue string 移动进闭包
            loop_->runInLoop([this, buf = std::move(buf)]() mutable {
                sendInLoop(std::move(buf));
            });
        }
    }
}

/**
 * @brief 【非线程安全的私有实现】在IO线程中执行的实际发送逻辑。
 * @details
 * 这个函数是 send() 的底层实现，只能在当前连接所属的 IO 线程中被调用（由 EventLoop 保证）。
 * 主要流程如下：
 * 1. 检查连接状态，如果已经断开则直接返回，不再发送数据。
 * 2. 如果输出缓冲区为空且当前没有监听写事件，尝试直接通过系统调用 write 发送数据到 socket，
 *    这样可以减少一次内存拷贝（避免先写到 outputBuffer_ 再发送）。
 * 3. 如果数据没有一次性发完（或第一次 write 就遇到内核缓冲区满），
 *    剩余的数据会追加到 outputBuffer_，并开始监听写事件（EPOLLOUT），
 *    等待下次 socket 可写时继续发送。
 * 4. 如果发送的数据量超过高水位标记（highWaterMark_），会触发高水位回调，
 *    通知应用层当前发送压力较大，可以做流控。
 * 5. 如果全部数据发送完毕且设置了写完成回调，会通过 queueInLoop 异步通知应用层，
 *    保证回调不会阻塞当前 IO 线程。
 * 6. 错误处理：如果 write 返回错误且不是 EWOULDBLOCK（缓冲区满），
 *    会记录日志并根据错误类型（如 EPIPE、ECONNRESET）标记连接故障。
 *
 * @param data 要发送的数据指针
 * @param len  数据长度（字节数）
 */
void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    // 如果之前调用过 shutdown, 则不能再发送新数据
    if (state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing!");
        return;
    }

    // 优化: 如果输出缓冲区为空, 尝试直接发送。
    // 这可以避免一次不必要的内存拷贝(从用户数据到outputBuffer_)
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            // 如果数据一次性发送完毕
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 调用用户的“写完成回调”。因为可能在回调里做耗时操作,
                // 所以使用queueInLoop确保在下一轮事件循环中执行, 不阻塞当前IO处理。
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else // nwrote < 0, write 出错
        {
            nwrote = 0;
            // EWOULDBLOCK 表示内核发送缓冲区已满, 是正常情况, 不算错误
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendInLoop");
                if (errno == EPIPE || errno == ECONNRESET) // 对端重置连接等错误
                {
                    faultError = true;
                }
            }
        }
    }

    // 如果数据没有一次性发完, 或者首次发送就遇到缓冲区满
    if (!faultError && remaining > 0)
    {
        size_t oldLen = outputBuffer_.readableBytes();
        // 检查是否达到高水位标记
        if (oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkCallback_)
        {
            // 触发高水位回调, 通知用户发送速度过快, 应用层应减缓发送
            loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
        }
        // 将剩余数据追加到 outputBuffer_
        outputBuffer_.append(static_cast<const char *>(data) + nwrote, remaining);
        if (!channel_->isWriting())
        {
            // 开始监听可写事件(EPOLLOUT), 以便在socket可写时, 内核能通知我们继续发送
            channel_->enableWriting();
        }
    }
}

void TcpConnection::sendInLoop(const std::string &message)
{
    // 调用我们已有的、健壮的 (void*, size_t) 版本
    sendInLoop(message.data(), message.size());
}

/**
 * @brief 【线程安全的公有接口】关闭连接 (半关闭)。
 * @details
 * "半关闭"是指关闭本端的写通道, 但仍然可以接收数据。
 * 这是实现优雅关闭(graceful shutdown)的标准做法。
 */
void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting); // 进入“正在关闭”状态
        // 同样, 将实际操作派发给IO线程执行
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

/**
 * @brief 【非线程安全的私有实现】在IO线程中执行的实际关闭逻辑。
 */
void TcpConnection::shutdownInLoop()
{
    // 只有当输出缓冲区的数据全部发送完毕后, 才能关闭写端。
    // 如果还在监听可写事件, 说明数据还没发完, handleWrite会接管关闭流程。
    if (!channel_->isWriting())
    {
        socket_->shutdownWrite(); // 调用底层的 shutdown(SHUT_WR)
    }
}

/**
 * @brief 【非线程安全】当连接成功建立后, 在其所属的IO线程中被调用。
 * @details
 * 这是连接生命周期的“正式开始”。
 */
void TcpConnection::connectEstablished()
{
    setState(kConnected); // 设置状态为“已连接”
    // 【核心安全机制】将 Channel 与 TcpConnection 的 shared_ptr 绑定。
    // 这确保了即使上层(TcpServer)已经释放了对这个TcpConnection的shared_ptr,
    // 只要Channel还活着(还在Poller的监听列表里), 这个TcpConnection对象就不会被析构。
    channel_->tie(shared_from_this());

    channel_->enableReading(); // 正式开始监听读事件

    // 执行用户设置的连接建立回调
    connectionCallback_(shared_from_this());
}

/**
 * @brief 【非线程安全】当连接被销毁前, 在其所属的IO线程中被调用。
 * @details
 * 这是连接生命周期的“最终清理”。
 */
void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll();                  // 停止所有事件监听
        connectionCallback_(shared_from_this()); // 执行用户设置的连接断开回调
    }
    channel_->remove(); // 将 Channel 从 Poller 中彻底移除
}

/**
 * @brief 【非线程安全】Channel 的读事件回调, 由 EventLoop 触发
 */
void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    // 从 socket 读取数据到 inputBuffer_
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0) // 成功读取到数据
    {
        // 调用用户的消息回调, 将数据和时间戳交给用户处理
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0) // read 返回0, 表示对端已正常关闭连接
    {
        handleClose();
    }
    else // n < 0, 表示出错
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}

/**
 * @brief 【非线程安全】Channel 的写事件回调, 由 EventLoop 触发
 */
void TcpConnection::handleWrite()
{
    if (channel_->isWriting()) // 确保仍在监听写事件
    {
        // 从 outputBuffer_ 向 socket 写入数据
        ssize_t n = ::write(channel_->fd(), outputBuffer_.peek(), outputBuffer_.readableBytes());
        if (n > 0)
        {
            outputBuffer_.retrieve(n);              // 从缓冲区消耗掉已发送的数据
            if (outputBuffer_.readableBytes() == 0) // 如果数据已全部发送完毕
            {
                // 【核心】必须停止监听写事件, 否则会因为socket一直可写而导致此回调被不停触发, 造成CPU 100% (busy-loop)。
                channel_->disableWriting();

                if (writeCompleteCallback_)
                {
                    // 调用用户的写完成回调, 通知用户数据已发完
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }

                // 如果当前状态是“正在关闭”, 那么在数据发完后, 执行半关闭
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite failed, errno:%d", errno);
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing", channel_->fd());
    }
}

/**
 * @brief 【非线程安全】Channel 的关闭事件回调, 或由 handleRead 调用
 * @details
 * 这是连接关闭的中央处理函数, 负责状态转换和回调通知。
 */
void TcpConnection::handleClose()
{
    setState(kDisconnected);
    channel_->disableAll(); // 停止监听任何事件

    TcpConnectionPtr connPtr(shared_from_this());
    // 执行用户的连接回调 (表示连接已断开)
    connectionCallback_(connPtr);
    // 【核心】执行TcpServer设置的关闭回调, 其作用是通知TcpServer将自己从连接列表中移除。
    closeCallback_(connPtr);
}

/**
 * @brief 【非线程安全】Channel 的错误事件回调
 */
void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = static_cast<socklen_t>(sizeof optval);
    int err = 0;
    // 使用 getsockopt(SO_ERROR) 获取底层的 socket 错误码, 这是处理socket异步错误的標準方法
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:[%s] - SO_ERROR = %d", name_.c_str(), err);
}