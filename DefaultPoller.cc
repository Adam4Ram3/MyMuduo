#include "Poller.h"
#include "EPollPoller.h" // 👈 需要包含具体 "epoll" 实现的头文件
// #include "PollPoller.h" // 👈 如果您也实现了 PollPoller, 也需要包含它的头文件

#include <stdlib.h> // 👈 需要包含此头文件以使用 ::getenv

/**
 * @brief Poller 的工厂函数, EventLoop 通过此函数创建具体的 Poller 实例。
 * * @param loop EventLoop 的指针, 用于 Poller 的构造。
 * @return Poller* 返回一个指向具体 Poller 实现的基类指针。
 */
Poller *Poller::newDefaultPoller(EventLoop *loop)
{
    // ::getenv 用于获取一个环境变量的值
    // 这是一个运行时选择机制, 允许用户通过设置环境变量来强制使用 poll
    if (::getenv("MUDUO_USE_POLL"))
    {
        // 如果用户在环境中设置了 MUDUO_USE_POLL=1 (或任何值)
        // 就返回一个 PollPoller 的实例 (这里我们先用 nullptr 占位)
        // return new PollPoller(loop); 
        return nullptr; // 假设您尚未实现 PollPoller
    }
    else
    {
        // 否则, 默认在 Linux 上返回 EPollPoller 的实例
        return new EPollPoller(loop);
    }
}