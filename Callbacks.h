#pragma once

#include <memory>
#include <functional>

// 前置声明, 降低头文件依赖
class Buffer;
class TcpConnection;
class Timestamp;

/**
 * @brief 使用 using 关键字定义了一系列类型别名, 统一整个项目中回调函数的类型和智能指针的名称。
 * 这是一个非常好的实践, 增强了代码的可读性和可维护性。
 */

// TcpConnection 的智能指针类型
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

// 连接回调函数: 用于处理新连接的建立和连接的断开
using ConnectionCallback = std::function<void(const TcpConnectionPtr &)>;

// 关闭回调函数: 通常由 TcpConnection 内部使用, 用于通知上层连接已关闭
using CloseCallback = std::function<void(const TcpConnectionPtr &)>;

// 写完成回调函数: 当所有数据都成功从应用层缓冲区写入到内核缓冲区后调用
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr &)>;

// 消息回调函数: 当有数据可读时调用, 是处理业务逻辑的核心
using MessageCallback = std::function<void(const TcpConnectionPtr &,
                                           Buffer *,
                                           Timestamp)>;

// 这是一个空的占位类, 当前文件中主要的作用是提供一个统一的头文件名,
// 所有的回调都定义在这里。
class Callbacks
{
public:
private:
};