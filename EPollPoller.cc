#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"

#include <errno.h>
#include <unistd.h>
#include <string.h>

// Channel未添加到Poller中
const int kNew = -1; // channel的成员index_ = -1
// Channel已添加到Poller中
const int kAdded = 1;
// Channel从Poller中删除
const int kDeleted = 2;

/**
 * @brief EPollPoller 构造函数
 * @param loop 所属的 EventLoop
 */
EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop),
      epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize)
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
}

/**
 * @brief EPollPoller 析构函数，关闭 epoll 文件描述符
 */
EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}

/**
 * @brief 等待IO事件，并填充活跃的 Channel 列表
 * @param timeoutMs 超时时间（毫秒）
 * @param activeChannels 输出参数，用于存储活跃的 Channel
 * @return 事件发生的时间戳
 */
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    // 实际上调用LOG_DEBUG更为合理
    LOG_INFO("func=%s => fd total count: %lu\n", __FUNCTION__, channels_.size());

    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno;

    Timestamp now(Timestamp::now());

    if (numEvents > 0)
    {
        LOG_INFO("%d events happened \n", numEvents);
        fillActiveChannels(numEvents, activeChannels);
        if (numEvents == static_cast<int>(events_.size()))
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("%s timeout! \n", __FUNCTION__);
    }
    else
    {
        if (saveErrno != EINTR)
        {
            errno = saveErrno;
            LOG_ERROR("EPollerPoller::poll() error!");
        }
    }

    return now;
    // // 实际上应该调用 epoll_wait
    // LOG_INFO("func=%s => fd total count:%lu\n", __FUNCTION__, channels_.size());

    // int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    // int saveErrno = errno;
    // Timestamp now(Timestamp::now());

    // if (numEvents > 0)
    // {
    //     LOG_INFO("%d events happened \n", numEvents);
    //     fillActiveChannels(numEvents, activeChannels);
    //     if (numEvents == events_.size())
    //     {
    //         events_.resize(events_.size() * 2);
    //     }
    // }
    // else if (numEvents == 0)
    // {
    //     LOG_DEBUG("%s timeout! \n", __FUNCTION__);
    // }
    // else
    // {
    //     if (saveErrno != EINTR)
    //     {
    //         errno = saveErrno;
    //         LOG_ERROR("EPollPoller::poll() err!");
    //     }
    // }
    // return now;
}

/**
 * @brief 在 Poller 中更新或添加 Channel
 * @param channel 需要更新的 Channel
 */
void EPollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();
    LOG_INFO("func=%s => fd=%d events=%d index=%d \n", __FUNCTION__, channel->fd(), channel->events(), index);

    if (index == kNew || index == kDeleted)
    {
        int fd = channel->fd();
        if (index == kNew)
        {
            // 只有当 Channel 是全新的，才需要将它添加到 map 中
            channels_[fd] = channel;
        }
        // 对于 kDeleted 状态，它已经存在于 map 中，所以不需要做任何 map 操作

        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else // Channel已经在该poller上注册过
    {
        // int fd = channel->fd();
        if (channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

/**
 * @brief 从 Poller 中移除 Channel
 * @param channel 需要移除的 Channel
 */
void EPollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    channels_.erase(fd);

    LOG_INFO("func=%s => fd=%d\n", __FUNCTION__, fd);

    int index = channel->index();
    if (index == kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}

/**
 * @brief 将 epoll_wait 返回的就绪事件填充到 activeChannels 列表中
 * @param numEvents 就绪事件的数量
 * @param activeChannels 输出参数，活跃的 Channel 列表
 */
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel); // EventLoop拿到了它的Poller给它返回的所有发生事件的channel列表
    }
}

/**
 * @brief 调用 epoll_ctl 来更新 epoll 内核事件表
 * @param operation 操作类型（ADD, MOD, DEL）
 * @param channel 目标 Channel
 */
void EPollPoller::update(int operation, Channel *channel)
{
    // epoll_event event;
    // memset(&event, 0, sizeof event);

    // int fd = channel->fd();
    // event.events = channel->events();
    // event.data.fd = fd;
    // event.data.ptr = channel;

    // if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    // {
    //     if (operation == EPOLL_CTL_DEL)
    //     {
    //         LOG_ERROR("epoll_ctl del error:%d\n", errno);
    //     }
    //     else
    //     {
    //         LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
    //     }
    // }
    epoll_event event;
    memset(&event, 0, sizeof event);

    int fd = channel->fd();
    event.events = channel->events();
    event.data.ptr = channel;
    // event.data.fd = fd;

    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error:%d\n", errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
        }
    }
}
