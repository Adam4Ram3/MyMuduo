#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"
#include <sys/epoll.h>
const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop),
      fd_(fd),
      events_(0),
      revents_(0),
      index_(-1),
      tied_(false)
{
}

Channel::~Channel()
{
}

void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj;
    tied_ = true;
}

void Channel::remove()
{
    // 实现 removeChannel 函数，通过 EventLoop 调用 Poller 的相应方法，移除 fd 的事件注册
    loop_->removeChannel(this);
}

/**
 * @brief 更新通道在事件循环中的事件注册。
 *
 * 此方法根据当前的事件兴趣（如读、写或错误事件），在事件循环中注册或修改与此通道关联的文件描述符。
 * 它确保通道被正确监控指定的感兴趣事件。
 */
void Channel::update()
{

    // 实现 updateChannel 函数，通过 EventLoop 调用 Poller 的相应方法，注册 fd 的 events 事件
    loop_->updateChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
    }
    else
    {
        handleEventWithGuard(receiveTime);
    }
}

//根据Poller通知的channel发生的具体事件，由channel负责具体的回调操作
void Channel::handleEventWithGuard(Timestamp reveiveTime)
{
    LOG_INFO("Channel handleEvent revents:%d\n",revents_);
    if((revents_&EPOLLHUP)&&!(revents_&EPOLLIN))
    {
        if(closeCallback_)
        {
            closeCallback_();
        }
    }

    if(revents_&EPOLLERR)
    {
        if(errorCallback_)
        {
            errorCallback_();
        }
    }
    if(revents_&(EPOLLIN|EPOLLPRI))
    {
        if(readCallback_)
        {
            readCallback_(reveiveTime);
        }
    }
    if(revents_&(EPOLLOUT))
    {
        if(writeCallback_)
        {
            writeCallback_();
        }
    }
}
