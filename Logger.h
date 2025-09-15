#pragma once

#include <string>
#include "noncopyable.h"

// 定义日志的级别 INFO ERROR FATAL DEBUG
enum LogLevel
{
    INFO,  // 普通信息
    ERROR, // 错误信息
    FATAL, // core信息
    DEBUG, // 调试信息
};

// 输出一个日志类
class Logger : noncopyable
{
public:
    // 获取日志唯一的实例对象
    static Logger &instance();

    // 设置日志级别
    void setLogLevel(LogLevel level);

    // 写日志核心接口，使用可变参数
    void log(LogLevel level, const char *format, ...);

private:
    LogLevel loglevel_;
    Logger() {} // 构造函数私有化
};

/********************************************************************************
 * 修改后的宏定义
 ********************************************************************************/

// 宏的核心思想：将所有参数原封不动地传递给 Logger::instance().log(...) 函数
#define LOG_INFO(logmsgFormat, ...)  Logger::instance().log(INFO, logmsgFormat, ##__VA_ARGS__)
#define LOG_ERROR(logmsgFormat, ...) Logger::instance().log(ERROR, logmsgFormat, ##__VA_ARGS__)
#define LOG_FATAL(logmsgFormat, ...) Logger::instance().log(FATAL, logmsgFormat, ##__VA_ARGS__)

#ifdef MUDEBUG
#define LOG_DEBUG(logmsgFormat, ...) Logger::instance().log(DEBUG, logmsgFormat, ##__VA_ARGS__)
#else
#define LOG_DEBUG(logmsgFormat, ...) // Release模式下，LOG_DEBUG宏展开为空，无任何开销
#endif