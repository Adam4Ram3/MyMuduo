#include "Socket.h"
#include "Logger.h"
#include "InetAddress.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <strings.h> // bzero
#include <netinet/tcp.h> // 包含了 TCP_NODELAY 选项

/**
 * @brief Socket 析构函数, 在对象销毁时自动关闭 socket 文件描述符。
 * @details
 * 这是 RAII (Resource Acquisition Is Initialization) 技法的核心体现。
 * 只要一个 Socket 对象被创建, 它就拥有了一个 sockfd 资源。当该对象
 * 的生命周期结束时(例如离开作用域), 析构函数会自动被调用, 确保
 * ::close() 被执行, 从而将文件描述符资源归还给操作系统, 
 * 完美地防止了资源泄漏。
 */
Socket::~Socket()
{
    ::close(sockfd_);
}

/**
 * @brief 将该 socket 绑定到一个本地的IP地址和端口上。
 * @param localaddr 包含IP地址和端口信息的 InetAddress 对象。
 * @details
 * bind() 是服务器启动流程中的关键一步。它就像是给餐厅挂上一个
 * 明确的地址和门牌号, 告诉操作系统:"所有访问这个IP和端口的TCP请求, 
 * 都应该由我这个socket来处理。"
 */
void Socket::bindAddress(const InetAddress &localaddr)
{
    // ::bind 是一个系统调用, 用于将 sockfd 与一个具体的地址关联起来。
    // 如果失败返回-1。
    if (0 != ::bind(sockfd_, (sockaddr *)localaddr.getSockAddr(), sizeof(sockaddr_in)))
    {
        // 如果绑定失败, 通常是严重错误 (如端口已被占用), 程序应终止。
        LOG_FATAL("bind sockfd:%d fail \n", sockfd_);
    }
}

/**
 * @brief 开始监听该 socket, 使其变为一个被动监听的 socket, 准备好接受连接。
 * @details
 * listen() 将一个主动的 socket 转换为被动的 "listening socket"。
 * 它是服务器从"打电话出去"的角色转变为"接听电话"的角色的关键一步。
 */
void Socket::listen()
{
    // ::listen 系统调用。第二个参数是 backlog, 表示内核为此 socket 维护的
    // "已完成三次握手但尚未被accept的连接"的队列大小。
    // 当这个队列满了之后, 新的连接请求可能会被拒绝。1024是一个常见的默认值。
    if (0 != ::listen(sockfd_, 1024))
    {
        // 监听失败是严重错误, 程序应终止。
        LOG_FATAL("listen sockfd:%d fail \n", sockfd_);
    }
}

/**
 * @brief 接受一个新的客户端连接。
 * @param peeraddr [输出参数] 用于接收并存储对端 (客户端) 的地址信息。
 * @return int 成功时返回一个新的、与客户端通信的 socket 文件描述符 (connfd)。
 * 失败时返回-1。
 */
int Socket::accept(InetAddress *peeraddr)
{
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    // 使用 bzero 初始化 addr, 避免潜在的垃圾数据
    ::bzero(&addr, sizeof(addr));

    // ::accept4 是 accept 的一个现代 Linux 扩展, 可以在接受连接的同时,
    // 原子地为返回的新 socket (connfd) 设置标志, 如非阻塞(SOCK_NONBLOCK)
    // 和执行后关闭(SOCK_CLOEXEC)。这避免了额外的 fcntl 系统调用, 效率更高。
    int connfd = ::accept4(sockfd_, (sockaddr *)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0)
    {
        // 如果成功, 将内核返回的对端地址信息, 设置到用户传入的 peeraddr 对象中。
        peeraddr->setSockAddr(addr);
    }
    else // connfd < 0
    {
        // accept 失败可能是多种原因, 有些是可恢复的(如被信号中断),
        // 所以这里记录为错误日志, 而不是致命日志。
        LOG_ERROR("accept err:%d \n", errno);
    }

    return connfd;
}

/**
 * @brief 关闭该 socket 的写半部分。
 * @details
 * 这是一种优雅关闭连接的方式。调用后, 本端无法再发送数据, 但仍然可以
 * 接收对端发来的数据。对端在读取完所有缓冲区数据后, 会收到一个 EOF (文件结束) 信号。
 * 常用于客户端发送完请求后, 告诉服务器"我的话说完了, 等你回答"。
 */
void Socket::shutdownWrite()
{
    // ::shutdown 系统调用可以精细地控制 socket 的关闭。
    // SHUT_WR 表示关闭写方向 (Write)。
    if (::shutdown(sockfd_, SHUT_WR) < 0)
    {
        LOG_ERROR("shutdownWrite error");
    }
}

/**
 * @brief 设置 TCP_NODELAY 选项 (禁用 Nagle 算法)。
 * @param on true 表示开启(禁用Nagle), false 表示关闭(启用Nagle)。
 * @details
 * Nagle算法会尝试将多个小的数据包组合成一个大的再发送, 以提高网络吞吐量。
 * 但这会引入延迟。对于需要低延迟的实时应用(如游戏、远程控制), 
 * 需要禁用Nagle算法, 确保小数据包能被立即发送。
 */
void Socket::setTcpNoDelay(bool on)
{
    int optval = on ? 1 : 0;
    // ::setsockopt 用于设置 socket 的各种选项。
    // IPPROTO_TCP: 选项所在的协议层 (TCP层)。
    // TCP_NODELAY: 选项名称。
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}

/**
 * @brief 设置 SO_REUSEADDR 选项 (地址复用)。
 * @param on true 表示开启, false 表示关闭。
 * @details
 * 开启此选项后, 允许服务器在主动关闭连接并处于 TIME_WAIT 状态时, 
 * 立即重启并重新绑定到同一个端口。
 * 这是服务器编程中一个几乎必须设置的选项, 以确保服务器能够快速重启。
 */
void Socket::setReuseAddr(bool on)
{
    int optval = on ? 1 : 0;
    // SOL_SOCKET: 选项所在的协议层 (通用Socket层)。
    // SO_REUSEADDR: 选项名称。
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}

/**
 * @brief 设置 SO_REUSEPORT 选项 (端口复用)。
 * @param on true 表示开启, false 表示关闭。
 * @details
 * 开启此选项后, 允许多个进程或线程绑定到同一个IP地址和端口上。
 * 内核会自动进行负载均衡, 将新连接分发给其中一个监听者。
 * 这是实现单机多进程/线程高性能服务器的一种常用技术。
 */
void Socket::setReusePort(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
}

/**
 * @brief 设置 SO_KEEPALIVE 选项 (TCP保活机制)。
 * @param on true 表示开启, false 表示关闭。
 * @details
 * 当连接长时间没有任何数据交互时, 开启此选项的TCP协议栈会自动
 * 发送探测包来检查对端是否仍然存活。
 * 这可以帮助服务器清理那些因为客户端崩溃、断网等原因而没有正常关闭的“死连接”,
 * 防止它们永久占用系统资源。
 */
void Socket::setKeepAlive(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}