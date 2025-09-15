#pragma once

#include "Poller.h"
#include "Timestamp.h"

#include <vector>
#include <sys/epoll.h>

// EPollPoller类，继承自Poller，是基于epoll的IO复用具体实现
class EPollPoller : public Poller
{
public:
    EPollPoller(EventLoop *loop); // 构造函数
    ~EPollPoller() override;     // 析构函数

    // 重写基类Poller的抽象方法
    Timestamp poll(int timeoutMs, ChannelList *activeChannels) override; // 等待IO事件
    void updateChannel(Channel *channel) override;                       // 在Poller中更新通道
    void removeChannel(Channel *channel) override;                       // 从Poller中移除通道

private:
    static const int kInitEventListSize = 16; // events_数组的初始大小

    // 填写活跃的连接
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;
    // 更新channel通道，实际调用epoll_ctl
    void update(int operation, Channel *channel);

    using EventList = std::vector<epoll_event>; // epoll_event的vector类型定义

    int epollfd_;     // epoll_create创建返回的fd
    EventList events_; // 用于存放epoll_wait返回的就绪事件的集合
};