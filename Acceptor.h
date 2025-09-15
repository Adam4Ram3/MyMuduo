#pragma once
#include "noncopyable.h"
#include "Socket.h"
#include "Channel.h"

#include <functional>

class InetAddress;
class EventLoop;
class Acceptor : noncopyable
{
public:
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress &)>;

    Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
    ~Acceptor();

    void setNewConnectionCallback(NewConnectionCallback cb) { newConnectionCallback_ = std::move(cb); }
    bool listening() const { return listening_; }
    void listen();

private:
    void handleRead();

    // EventLoop *loop_; // Acceptor用的就是用户定义的baseLoop，也就是mainLoop
    Socket acceptSocket_;
    Channel acceptChannel_;
    NewConnectionCallback newConnectionCallback_;
    bool listening_;
};
