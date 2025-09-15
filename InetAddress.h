#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include <cstring> 

// 封装socket地址类型
class InetAddress
{
public:
    // InetAddress() { std::memset(&addr_, 0, sizeof addr_); } // 新增：默认构造，便于 accept 时填充
    explicit InetAddress(uint16_t port=0, std::string ip = "127.0.0.1");
    explicit InetAddress(const sockaddr_in &addr)
        : addr_(addr)
    {
    }

    std::string toIp() const;
    std::string toIpPort() const;
    uint16_t toPort() const;

    void setSockAddr(const sockaddr_in &addr) { addr_ = addr; }
    const sockaddr_in *getSockAddr() const { return &addr_; }

private:
    sockaddr_in addr_;
};