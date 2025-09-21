#include <string>
#include <functional>

// 关键: 我们现在像使用一个标准的第三方库一样, 包含安装在系统中的头文件
#include <mymuduo/TcpServer.h>
#include <mymuduo/Logger.h>

class EchoServer
{
public:
    EchoServer(EventLoop *loop,
               const InetAddress &addr,
               const std::string &name)
        : server_(loop, addr, name),
          loop_(loop)
    {
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));

        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        server_.setThreadNum(3);
    }

    void start()
    {
        server_.start();
    }

private:
    void onConnection(const TcpConnectionPtr &conn)
    {
        if (conn->connected())
        {
            LOG_INFO("Connection UP : %s", conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("Connection DOWN : %s", conn->peerAddress().toIpPort().c_str());
        }
    }

    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time)
    {
        std::string msg = buf->retrieveAllAsString();
        conn->send(msg);
    }

    TcpServer server_;
    EventLoop *loop_;
};

int main()
{
    EventLoop loop;
    InetAddress addr(9999); // 使用一个新端口, 如9999, 避免和之前的测试冲突
    // 修改服务器名称以作区分
    EchoServer server(&loop, addr, "EchoServer-InstalledTest"); 
    server.start();
    loop.loop();
    return 0;
}