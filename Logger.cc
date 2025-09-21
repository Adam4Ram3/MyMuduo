#include "Logger.h"
#include "Timestamp.h"     // 👈 【新增】包含时间戳头文件
#include "CurrentThread.h" // 👈 【新增】包含当前线程信息头文件
#include <iostream>
#include <cstdarg> // C风格可变参数所需的头文件
#include <cstdlib>
#include <mutex>

// C++11 后，静态成员的初始化可以更简单
// Logger* Logger::instance_ = nullptr; // 如果使用旧的单例模式

// 获取日志唯一的实例对象 (这里使用C++11的Magic Static实现线程安全的单例)
Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::setLogLevel(LogLevel level)
{
    loglevel_ = level;
}

// 写日志核心接口的实现
void Logger::log(LogLevel level, const char *format, ...)
{
    // 使用 lock_guard 保证日志输出的线程安全
    static std::mutex s_mutex;
    std::lock_guard<std::mutex> lock(s_mutex);

    // 如果当前消息的级别低于Logger设置的级别, 则不记录
    if (level < loglevel_)
    {
        return;
    }

    // 【改造一】构建日志消息的前缀: [时间戳 tid] [日志级别]
    std::cout << Timestamp::now().toString()     // 输出当前时间
              << " tid:" << CurrentThread::tid() // 输出当前线程ID
              << " ";

    switch (level)
    {
    case INFO:
        std::cout << "[INFO] ";
        break;
    case ERROR:
        std::cout << "[ERROR] ";
        break;
    case FATAL:
        std::cout << "[FATAL] ";
        break;
    case DEBUG:
        std::cout << "[DEBUG] ";
        break;
    default:
        break;
    }

    char buf[1024] = {0};

    // 使用 va_list 等C风格API来处理可变参数
    va_list args;
    va_start(args, format);
    // 使用 vsnprintf 将用户传入的格式化字符串和参数写入buf
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    // 【改造二】输出格式化后的用户消息, 并换行
    std::cout << buf << std::endl;

    // 如果是FATAL级别的日志, 记录后终止程序
    if (level == FATAL)
    {
        exit(-1);
    }
}