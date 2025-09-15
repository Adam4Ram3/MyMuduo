#include "Acceptor.h"
#include "Logger.h"
#include "InetAddress.h"
#include "Socket.h" // 虽然源文件中没有直接使用Socket的方法, 但构造函数中acceptSocket_的创建依赖其定义

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

/**
 * @brief 创建一个非阻塞的、用于监听的TCP socket。
 * 这是一个静态辅助函数, 仅在Acceptor的构造函数中使用。
 * @return int 返回创建好的socket文件描述符。
 */
static int createNonblocking()
{
    // ::socket 系统调用创建一个新的 socket
    // AF_INET: 使用 IPv4 协议
    // SOCK_STREAM: 使用 TCP 协议
    // SOCK_NONBLOCK: (Linux 2.6.27+ 扩展) 在创建时就将 socket 设置为非阻塞模式
    // SOCK_CLOEXEC: (Linux 2.6.27+ 扩展) 在 execve 执行新程序时自动关闭此文件描述符, 防止fd泄漏
    // 0: 协议族中的特定协议, 对于TCP, 设为0即可
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd < 0)
    {
        // 如果创建失败, 这是致命错误, 记录日志并终止程序
        LOG_FATAL("%s:%s:%d listen socket create err:%d \n", __FILE__, __FUNCTION__, __LINE__, errno);
    }
    return sockfd; // 返回创建的文件描述符
}

/**
 * @brief Acceptor 的构造函数。
 * @param loop Acceptor 所属的 EventLoop (通常是 mainLoop)。
 * @param listenAddr 服务器需要监听的IP地址和端口。
 * @param reuseport 是否开启端口复用 (SO_REUSEPORT)。
 */
Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : //loop_(loop), //【已在您上一步修复】这里不再需要保存loop_, 因为它只在构造Channel时使用一次
      acceptSocket_(createNonblocking()), // 创建监听socket, 并用Socket类进行封装, 实现RAII
      acceptChannel_(loop, acceptSocket_.fd()), // 创建一个Channel, 专门负责监听 acceptSocket_ 上的事件
      listening_(false) // 初始状态为未监听
{
    // 设置服务器socket的标准选项
    acceptSocket_.setReuseAddr(true);   // 开启地址复用
    acceptSocket_.setReusePort(reuseport); // 根据传入参数决定是否开启端口复用
    
    // 将socket与指定的IP和端口进行绑定
    acceptSocket_.bindAddress(listenAddr);

    // 【核心绑定】设置 acceptChannel_ 的读事件回调函数为 Acceptor::handleRead
    // 当监听的socket上有新连接到来时(可读事件), EventLoop就会调用这个 handleRead 方法
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

/**
 * @brief Acceptor 的析构函数。
 * 负责在对象销毁前, 清理Channel在Poller中的注册信息。
 * Socket对象的析构由其自身的析构函数负责, 这里无需手动处理。
 */
Acceptor::~Acceptor()
{
    // 将Channel关心的所有事件都禁用
    acceptChannel_.disableAll();
    // 将Channel从Poller的监听列表中彻底移除
    acceptChannel_.remove();
}

/**
 * @brief 启动监听。
 * 这是让服务器开始接受新连接的入口函数。
 */
void Acceptor::listen()
{
    listening_ = true; // 将状态标记为正在监听
    acceptSocket_.listen(); // 调用底层socket的listen()方法, 使其进入被动监听状态
    acceptChannel_.enableReading(); // 【核心】将 acceptChannel_ 注册到Poller中, 开始监听新连接事件(EPOLLIN)
}

/**
 * @brief 处理监听socket上的读事件(即新连接的到来)。
 * 这个函数是 acceptChannel_ 的回调函数, 由 EventLoop 在检测到新连接时调用。
 */
void Acceptor::handleRead()
{
    InetAddress peerAddr; // 用于接收客户端的地址信息
    
    // 调用 acceptSocket_ 的 accept 方法, 接受新连接并返回一个用于通信的 connfd
    int connfd = acceptSocket_.accept(&peerAddr);

    if (connfd >= 0) // 成功接受一个新连接
    {
        // 检查用户是否设置了“新连接回调”函数
        if (newConnectionCallback_)
        {
            // 如果设置了, 就调用它, 将新连接的 connfd 和客户端地址 peerAddr 交给上层(通常是TcpServer)处理
            // 【任务交接】Acceptor 的工作到此结束, 后续的数据处理完全交给 TcpServer 和 TcpConnection
            newConnectionCallback_(connfd, peerAddr);
        }
        else
        {
            // 如果用户没有设置回调, 说明无法处理这个新连接,
            // 必须立即关闭 connfd, 否则会造成文件描述符泄漏
            ::close(connfd);
        }
    }
    else // accept 失败
    {
        // 记录错误日志
        LOG_ERROR("%s:%s:%d accept err:%d \n", __FILE__, __FUNCTION__, __LINE__, errno);
        // 如果错误是 EMFILE, 意味着文件描述符耗尽
        if (errno == EMFILE)
        {
            LOG_ERROR("%s:%s:%d sockfd reached limit\n", __FILE__, __FUNCTION__, __LINE__);
            // 在这里可以添加更高级的错误处理逻辑, 以避免服务器因fd耗尽而陷入“忙循环”
        }
    }
}