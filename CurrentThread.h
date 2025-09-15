#pragma once
#include <unistd.h>      // 用于 getpid() 等系统调用
#include <sys/syscall.h> // 用于 syscall(SYS_gettid)

// CurrentThread 命名空间，封装线程相关的工具函数
namespace CurrentThread
{
    // 下面这行的意思是：
    // extern：这个变量在别的地方定义，这里只是声明，可以在多个文件中共享
    // __thread：这是一个“线程局部变量”，每个线程都有自己独立的一份，不会互相影响
    // 用来保存当前线程的 tid（线程ID），避免每次都去系统查询
    extern __thread int t_cachedTid;

    // 这个函数会把当前线程的 tid（线程ID）查出来，并保存到 t_cachedTid 变量里
    void cacheTid();

    // inline：建议编译器把这个函数直接展开到调用处，提高效率
    // 这个函数用来获取当前线程的 tid（线程ID）
    // 如果 t_cachedTid 还没保存（为0），就调用 cacheTid() 查一次
    // __builtin_expect 是 GCC 的一个优化指令，告诉编译器 if 里的条件一般不成立，可以优化分支预测
    inline int tid()
    {
        if(__builtin_expect(t_cachedTid == 0, 0)) // 一般情况下 t_cachedTid 已经有值，这里只是优化编译器
        {
            cacheTid(); // 如果还没查过，就查一次
        }
        return t_cachedTid; // 返回当前线程的 tid
    }
}