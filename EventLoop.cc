#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>
// 防止一个线程创建多个EventLoop对象
thread_local EventLoop *t_loopInThisThread = nullptr;

// 定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000;

// 创建wakefd，用来notify唤醒subReactor处理新来的Channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error:%d \n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      threadId_(CurrentThread::tid()),           // ← 按声明顺序排在 callingPendingFunctors_ 之前
      poller_(Poller::newDefaultPoller(this)),
      wakeupFd_(createEventfd()),
      wakeupChannel_(new Channel(this, wakeupFd_)),
      callingPendingFunctors_(false)             // ← 现在排在最后，和声明顺序一致
{
    LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d\n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }

    // 设置wakeupFd的事件类型，以及发生事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个eventLoop都将监听wakeupChannel的EPOLLIN读事件
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();

    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping\n", this);

    while (!quit_)
    {
        activeChannels_.clear();
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            channel->handleEvent(pollReturnTime_);
        }
        // 执行当前EventLoop事件循环需要处理的回调操作
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping\n", this);
    looping_ = false;
}

/**
 * @brief 请求退出事件循环
 *
 * @details
 * 这是一个线程安全函数，可以由任何线程调用来请求 EventLoop 退出。
 * 它的设计核心是“优雅退出”，即不是立即终止，而是向事件循环发出一个“该结束了”的信号，
 * EventLoop 会在处理完当前一轮的事件后，在下一次循环开始前检查这个信号，然后干净地退出 loop() 函数。
 *
 * @note
 * 1. 退出不是立即的，而是在事件循环的下一次迭代中发生。
 * 2. 此函数的核心是线程安全的，因为它使用了原子变量 quit_ 和线程安全的 wakeup 机制。
 * 3. 如果是由其他线程调用，会通过 wakeup() 机制来中断 Poller::poll() 的阻塞，以确保退出请求能够被及时响应。
 */
void EventLoop::quit()
{
    // 核心动作：将退出标志位设置为true。
    // 这是一个原子操作，保证了在多线程环境下的可见性和无竞争。
    // EventLoop::loop() 函数的主循环条件是 while(!quit_)，
    // 当这个标志位变为true后，循环将在下一次检查时终止。
    // 比喻：就像在总控制室里，把“系统运行”的开关拨到了“准备停止”的位置。
    quit_ = true;

    // 关键的线程安全检查：判断调用quit()的当前线程，是否为EventLoop对象所属的IO线程。
    // 如果是EventLoop自己的IO线程调用quit()（例如在某个回调函数里），
    // 那么它本身就是醒着的，正在执行代码，不需要额外唤醒。
    // 只有当“外部”的其他线程请求退出时，我们才需要一个额外的唤醒操作来中断poll()的阻塞。
    if (!isInLoopThread())
    {
        // “按响门铃”：唤醒可能正阻塞在 Poller::poll() 上的IO线程。
        // 如果不执行wakeup()，那个IO线程可能会因为长时间没有网络事件而继续“沉睡”，
        // 最长可能要等待 kPollTimeMs（例如10秒）后才会醒来检查 quit_ 标志，导致退出延迟。
        // wakeup() 通过向 eventfd 写入数据来产生一个I/O事件，
        // 从而立即中断 poll() 的阻塞，确保退出请求能够被迅速响应。
        wakeup();
    }
}

void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread()) // 在当前的loop线程中，执行cb
    {
        cb();
    }
    else // 在非当前线程的loop中执行cb，就需要唤醒loop所在线程，执行cb
    {
        queueInLoop(cb);
    }
}

/**
 * @brief 将一个任务放入任务队列中, 等待EventLoop在其IO线程中执行。
 *
 * @details
 * 这是一个线程安全的函数, 任何线程都可以调用它来请求EventLoop执行一个任务。
 * 它总是将任务放入队列, 而不会立即执行, 具体的执行时机是在IO线程的下一轮循环中。
 * 此函数是实现“将工作从其他线程转移到IO线程”的核心机制。
 *
 * @param cb 需要在IO线程中执行的回调任务(一个std::function<void()>对象)。
 * @note
 * 1. 此函数是完全线程安全的, 内部使用了互斥锁来保护任务队列。
 * 2. 它包含了精妙的唤醒逻辑, 以确保IO线程能及时处理新加入的任务, 而不是要等到poll超时。
 */
void EventLoop::queueInLoop(Functor cb)
{
    // 使用一个代码块来限定锁的作用域, 这是一个很好的RAII实践。
    // 当 lock 对象离开这个作用域时, 它会自动析构, 从而自动释放互斥锁。
    // 这大大减小了互斥锁的持有时间。
    {
        // 使用 std::unique_lock 自动管理互斥锁 mutex_ 的生命周期。
        std::unique_lock<std::mutex> lock(mutex_);
        // 将任务添加到待办任务队列 pendingFunctors_ 的末尾。
        // emplace_back 通常比 push_back 更高效, 因为它可以在vector内部直接构造对象。
        pendingFunctors_.emplace_back(std::move(cb)); // 使用std::move避免不必要的拷贝
    }

    // 唤醒相应的、需要执行上面回调操作的loop线程。
    // 这里的 if 判断非常关键, 包含了两种需要唤醒的场景:
    // 1. !isInLoopThread(): 如果是其他线程在调用 queueInLoop, 那么目标IO线程很可能正阻塞在
    //    Poller::poll() 上, 必须通过 wakeup() 来唤醒它, 否则任务将无法被及时处理。
    // 2. callingPendingFunctors_: 如果是IO线程自己调用 queueInLoop (例如在一个待办任务A中又添加了
    //    一个新的待办任务B), 此时IO线程虽然是醒着的, 但它正在处理当前轮次的任务列表。
    //    如果不唤醒, 新加入的任务B就要等到下一次poll超时或IO事件才能被执行。
    //    通过wakeup(), 能让poll()在这一轮任务执行完后立即返回, 从而使下一轮循环马上开始,
    //    确保了任务B的及时执行。
    if (!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup(); // "按响门铃", 唤醒IO线程
    }
}

void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if(n!=sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu byte instead of 8\n",n);
    }

}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() reads %d bytes instead of 8\n", n);
    }
}

void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}
void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}
bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}
/**
 * @brief 执行任务队列(pendingFunctors_)中的所有待办任务。
 *
 * @details
 * 这个函数在 EventLoop::loop() 的每一次循环中, 在处理完I/O事件之后被调用。
 * 它负责执行所有由其他线程通过 queueInLoop() 投递过来的回调任务。
 *
 * @note
 * 1. 此函数不是线程安全的, 它必须且仅能在其所属的IO线程中被调用。
 * 2. 它使用了一个非常巧妙的 swap 技巧, 将待办任务转移到局部变量中,
 * 这极大地减小了互斥锁的锁定时间, 提高了并发性能。其他线程在IO线程处理
 * 任务时, 仍然可以顺畅地向任务队列中添加新任务。
 */
void EventLoop::doPendingFunctors()
{
    // 创建一个局部的functor列表, 用于和成员变量 pendingFunctors_ 进行交换。
    std::vector<Functor> functors;

    // 设置标志位为true, 表明当前IO线程正在处理待办任务。
    // 这个标志位用于 queueInLoop 中的唤醒逻辑判断。
    callingPendingFunctors_ = true;

    // 再次使用代码块来最小化锁的作用域
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 核心技巧: swap。这是一个非常快速的操作(O(1)时间复杂度), 
        // 它只是交换了两个vector的内部指针等数据, 而不是逐个拷贝元素。
        // 交换后, pendingFunctors_变为空, 可以立即释放锁, 
        // 让其他线程可以无阻塞地继续调用 queueInLoop 添加新任务。
        functors.swap(pendingFunctors_);
    }

    // 在锁已经释放的情况下, 安全地遍历并执行局部列表中的所有任务。
    // 这确保了即使某个回调任务执行时间很长, 也不会长时间地阻塞其他线程。
    for (const Functor &functor : functors)
    {
        functor(); // 执行回调操作
    }

    // 任务处理完毕, 重置标志位。
    callingPendingFunctors_ = false;
}
