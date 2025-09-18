#include "TcpServer.h"
#include "Logger.h"
#include "TcpConnection.h"

#include <functional>
#include <strings.h> // bzero in older systems

/**
 * @brief 一个辅助函数, 用于检查传入的EventLoop指针是否为空。
 * @details 在TcpServer的构造函数初始化列表中使用, 确保主EventLoop(baseLoop)是有效的。
 * 这是一个防御性编程的实践, 采用“快速失败”(Fail-Fast)的策略,
 * 如果传入了空指针, 程序会立即以FATAL日志终止, 防止在后续操作中出现难以调试的空指针解引用错误。
 * @param loop 待检查的EventLoop指针。
 * @return EventLoop* 如果指针有效, 则原样返回。
 */
static EventLoop *checkLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

/**
 * @brief TcpServer 的构造函数。
 * @details
 * 这是TcpServer对象的“组装”阶段。它会初始化所有成员变量,
 * 并创建核心的子组件, 如Acceptor(连接接收器)和EventLoopThreadPool(IO线程池)。
 * 同时, 它会建立Acceptor和TcpServer之间的回调连接。
 * @param loop 用户创建的 mainLoop (主Reactor), 负责接受新连接。
 * @param listenAddr 服务器监听的IP地址和端口。
 * @param nameArg 服务器的名称, 也会作为线程池中线程名称的前缀。
 * @param option 是否开启端口复用(SO_REUSEPORT)。
 */
TcpServer::TcpServer(EventLoop *loop,
                     const InetAddress &listenAddr,
                     const std::string &nameArg,
                     Option option)
    : loop_(checkLoopNotNull(loop)),                                   // 验证并保存mainLoop指针
      ipPort_(listenAddr.toIpPort()),                                  // 保存监听地址的字符串表示
      name_(nameArg),                                                  // 保存服务器名称
      acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)), // 创建Acceptor
      threadPool_(new EventLoopThreadPool(loop, name_)),               // 创建IO线程池
      connectionCallback_(),                                           // 默认初始化连接回调
      messageCallback_(),                                              // 默认初始化消息回调
      started_(0),                                                     // 原子计数器, 用于防止start()被多次调用
      nextConnId_(1)                                                   // 连接ID从1开始计数
{
    // 【核心回调设置】
    // 将Acceptor的“新连接到来”事件, 绑定到TcpServer自己的newConnection方法上。
    // 当Acceptor成功accept一个新连接时, 就会调用TcpServer::newConnection。
    // 这里使用std::bind将成员函数和this指针绑定, 并用占位符_1, _2来预留未来由Acceptor传入的参数位置。
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this,
                                                  std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer()
{
}

/**
 * @brief 设置 subLoop (I/O工作线程) 的数量。
 * @param numThreads I/O线程的数量。
 * @note 如果 numThreads 为 0, 服务器将工作在单线程模式下, 所有操作都在 mainLoop 中进行。
 */
void TcpServer::setThreadNum(int numThreads)
{
    threadPool_->setThreadNum(numThreads);
}

/**
 * @brief 启动服务器, 开始监听端口。
 * @details 这是一个线程安全的操作, 内部通过原子变量保证只启动一次。
 */
void TcpServer::start()
{
    // 使用原子变量的 fetch-add 操作, 保证start()的逻辑只被执行一次。
    if (started_++ == 0)
    {
        // 1. 启动线程池。这会创建并运行所有subLoop线程, 它们将阻塞在自己的loop()中等待任务。
        threadPool_->start(threadInitCallback_);

        // 2. 开启Acceptor的监听。acceptor_->listen()方法必须在mainLoop中执行。
        //    使用runInLoop可以保证即使start()是在其他线程被调用的, listen()也能安全地在mainLoop线程中执行。
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}

/**
 * @brief 【在mainLoop中执行】处理一个新到来的客户端连接。
 * @details 这个函数是Acceptor的NewConnectionCallback, 由Acceptor在成功accept一个新连接后调用。
 * @param sockfd 新连接的socket文件描述符。
 * @param peerAddr 客户端的地址信息。
 */
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    // 【因为TcpConnection还未完成, 函数体暂时为空】
    // 这是一个临时的实现, 以便通过编译器的严格检查。
    // (void)sockfd; 的写法是向编译器明确表示, 我知道这个参数存在, 但我暂时故意不用它。
    // 这是消除 "-Wunused-parameter" 警告/错误的常用方法。
    // (void)sockfd;
    // (void)peerAddr;

    /**
     * 【未来的完整逻辑】
     * 1. 从线程池中轮询一个subLoop: EventLoop* ioLoop = threadPool_->getNextLoop();
     * 2. 为新连接创建一个唯一的名称。
     * 3. 创建一个新的TcpConnection对象:
     * TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
     * 4. 将TcpServer设置好的用户回调(connectionCallback_, messageCallback_)设置给这个新的conn。
     * 5. 设置conn自己的关闭回调, 用于从TcpServer的map中移除自己: conn->setCloseCallback(...)。
     * 6. 将新的conn添加到TcpServer的ConnectionMap中进行管理。
     * 7. 调用ioLoop->runInLoop(), 在选中的subLoop线程中执行TcpConnection::connectEstablished。
     */
    EventLoop *ioLoop = threadPool_->getNextLoop();
    char buf[64] = {0};
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;

    LOG_INFO("TcpServer::connection [%s] - new connection [%s] from %s",
             name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

    // 通过sockfd获取其绑定的本机的ip地址和端口信息
    sockaddr_in local;
    ::memset(&local, 0, sizeof(local));
    socklen_t addrlen = sizeof(local);
    if (::getsockname(sockfd, (sockaddr *)&local, &addrlen) < 0)
    {
        LOG_ERROR("sockets::getLocalAddr");
    }

    InetAddress localAddr(local);
    TcpConnectionPtr conn(new TcpConnection(ioLoop,
                                            connName,
                                            sockfd,
                                            localAddr,
                                            peerAddr));
    connections_[connName] = conn;
    // 下面的回调都是用户设置给TcpServer => TcpConnection的，至于Channel绑定的则是TcpConnection设置的四个，handleRead,handleWrite... 这下面的回调用于handlexxx函数中
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置了如何关闭连接的回调
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

    ioLoop->runInLoop(
        std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    loop_->runInLoop(
        std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s\n",
             name_.c_str(), conn->name().c_str());

    connections_.erase(conn->name());
    EventLoop *ioLoop = conn->getLoop();
    ioLoop->queueInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn));
}
