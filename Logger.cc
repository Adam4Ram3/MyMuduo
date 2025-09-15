#include "Logger.h"
#include <iostream>
#include <cstdarg> // C风格可变参数所需的头文件
#include <cstdlib>

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
    // 如果当前消息的级别低于Logger设置的级别，则不记录
    if (level < loglevel_)
    {
        return;
    }

    char buf[1024] = {0};

    // 使用 va_list 等C风格API来处理可变参数
    va_list args;
    va_start(args, format);
    // 使用 vsnprintf 将格式化的字符串写入buf
    // vsnprintf 是 snprintf 的可变参数版本
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    // 在这里，你可以将 buf 写入文件、数据库或网络
    // 我们用一个简单的 cout 示例
    // 实际项目中，这里会添加时间戳、线程ID等信息
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

    std::cout << buf << std::endl;

    // ==========================================================
    // ==                  在这里增加新逻辑                     ==
    // ==========================================================
    if (level == FATAL)
    {
        // 记录完 FATAL 级别的日志后，强制退出程序
        exit(-1);
    }
}