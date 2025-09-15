#include "EventLoopThread.h"
#include "EventLoop.h"


EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
                                 const std::string &name)
    : loop_(nullptr),
      exiting_(false),
      thread_(std::bind(&EventLoopThread::threadFunc, this), name),
      mutex_(),
      cond_(),
      callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr)
    {
        loop_->quit();
        thread_.join();
    }
}

EventLoop* EventLoopThread::startLoop()
{
    thread_.start(); //启动底层的新线程

    EventLoop *loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while(loop_==nullptr)
        {
            cond_.wait(lock);
        }
        loop = loop_;
    }
    return loop;
}

//下面这个方法，是在单独的新线程当中运行的
void EventLoopThread::threadFunc()
{
    // 1. 创建一个独立的eventloop, 和当前线程一一对应
    EventLoop loop;

    // 2. 如果有初始化回调，则执行
    if (callback_)
    {
        callback_(&loop);
    }

    // 3. 将loop指针交给主线程，并唤醒主线程
    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }
    
    // 4. 【【最关键的一步】】开始事件循环
    //    线程会阻塞在这一行，不断地等待和处理事件
    loop.loop(); 

    // 5. (loop.loop() 退出后) 清理工作
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr; // 表明 loop 已销毁, 防止主线程使用悬空指针
}