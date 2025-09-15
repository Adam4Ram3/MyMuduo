#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"
EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg)
    : baseLoop_(baseLoop),
      name_(nameArg),
      started_(false),
      numThreads_(0),
      next_(0)
{
}

void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    started_ = true;
    for (int i = 0; i < numThreads_; i++)
    {
        // 修改前:
        // char buf[name_.size() + 32];

        // 修改后 (推荐):
        char buf[128]; // 使用一个足够大的固定尺寸缓冲区
        snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);
        EventLoopThread *t = new EventLoopThread(cb, buf);
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));
        loops_.push_back(t->startLoop()); // 底层创建线程，绑定一个新的EventLoop，并返回该Loop的地址
    }

    // 整个服务端只有一个线程运行着baseLoop
    if (numThreads_ == 0 && cb)
    {
        cb(baseLoop_);
    }
}

EventLoop *EventLoopThreadPool::getNextLoop()
{
    EventLoop *loop = baseLoop_;

    if (!loops_.empty())
    {
        loop = loops_[next_]; // 通过轮询获得处理下一个事件的loop
        ++next_;
        // 修改前:
        // if (next_ >= loops_.size())

        // 修改后 (推荐):
        if (static_cast<size_t>(next_) >= loops_.size())
        {
            next_ = 0;
        }
    }
    return loop;
}

std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
    if (loops_.empty())
    {
        return std::vector<EventLoop *>(1, baseLoop_);
    }
    else
    {
        return loops_;
    }
}
