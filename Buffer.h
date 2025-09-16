#pragma once

#include <vector>
#include <string>
#include <algorithm> // for std::copy
#include <cassert>   // for assert

// 网络库底层的缓冲区类型定义
//
// +-------------------+------------------+------------------+
// | prependable bytes |  readable bytes  |  writable bytes  |
// |                   |     (CONTENT)    |                  |
// +-------------------+------------------+------------------+
// |                   |                  |                  |
// 0      <=      readerIndex_    <=    writerIndex_    <=     size()
//
class Buffer
{
public:
    // kCheapPrepend 是预留给消息头的空间。比如我们可以在数据前添加一个4字节的长度信息。
    // 这样就无需在发送数据时，先申请一块新内存来组合消息头和消息体了。
    static const size_t kCheapPrepend = 8;
    // 缓冲区的初始大小
    static const size_t kInitialSize = 1024;

    /**
     * @brief 构造函数, 创建一个带有预留空间的缓冲区。
     * @param initialSize 缓冲区的初始容量。
     */
    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize),
          readerIndex_(kCheapPrepend),
          writerIndex_(kCheapPrepend)
    {
    }

    // --- 以下是您已完成的 '只读' 接口 ---

    size_t readableBytes() const
    {
        return writerIndex_ - readerIndex_;
    }

    size_t writableBytes() const
    {
        return buffer_.size() - writerIndex_;
    }

    size_t prependableBytes() const
    {
        return readerIndex_;
    }

    // 获取可读数据的起始地址
    const char *peek() const
    {
        return begin() + readerIndex_;
    }

    // --- 以下是需要补全的 '消费/写入/读取' 核心接口 ---

    /**
     * @brief 在可读数据区域的前端, 前置添加数据 (用于添加协议头)。
     * @details
     * 这是一个非常高效的操作, 它利用了缓冲区头部预留的 prependable 空间,
     * 避免了为了添加协议头而重新分配内存和拷贝整个消息体的昂贵开销。
     * @param data 指向要添加的数据的指针。
     * @param len 要添加的数据的长度。
     */
    void prepend(const void *data, size_t len)
    {
        // 1. 安全检查: 确保要添加的头部长度, 不超过当前头部预留空间的大小。
        assert(len <= prependableBytes());

        // 2. 核心操作: 将读指针向前移动 len 个位置, "腾出"空间。
        readerIndex_ -= len;

        // 3. 将头部数据拷贝到腾出的新空间中。
        // begin() + readerIndex_ 就是新空间的起始地址。
        const char *d = static_cast<const char *>(data);
        std::copy(d, d + len, begin() + readerIndex_);
    }
    /**
     * @brief "消费"掉len字节的数据。
     * 实际上只是移动读指针, 这是一个O(1)的操作, 非常高效。
     * @param len 要消费的数据长度。
     */
    void retrieve(size_t len)
    {
        // 断言确保要消费的长度不大于可读数据长度
        assert(len <= readableBytes());
        if (len < readableBytes())
        {
            readerIndex_ += len; // 只是移动读指针, 不进行内存操作
        }
        else // len == readableBytes()
        {
            retrieveAll(); // 如果全部消费完, 直接重置指针, 效率更高
        }
    }

    /**
     * @brief 消费掉所有可读数据。
     */
    void retrieveAll()
    {
        // 将读写指针重置到初始位置, 缓冲区清空
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }

    /**
     * @brief 将所有可读数据作为字符串返回, 并清空缓冲区。
     * @return std::string 包含所有可读数据的字符串。
     */
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes());
    }

    /**
     * @brief 将len字节的可读数据作为字符串返回, 并消费掉这部分缓冲区。
     * @param len 要转换并消费的数据长度。
     * @return std::string 包含指定长度数据的字符串。
     */
    std::string retrieveAsString(size_t len)
    {
        assert(len <= readableBytes());
        std::string result(peek(), len);
        retrieve(len); // 从缓冲区中移除已读取的数据
        return result;
    }

    /**
     * @brief 确保缓冲区有足够的可写空间。
     * @param len 需要写入的数据长度。
     */
    void ensureWritableBytes(size_t len)
    {
        if (writableBytes() < len)
        {
            makeSpace(len); // 如果可写空间不足, 则进行空间整理或扩容
        }
        assert(writableBytes() >= len);
    }

    /**
     * @brief 向缓冲区的可写部分追加数据。
     * @param data 要追加的数据的指针。
     * @param len 数据的长度。
     */
    void append(const char *data, size_t len)
    {
        ensureWritableBytes(len);
        std::copy(data, data + len, begin() + writerIndex_);
        writerIndex_ += len;
    }

    /**
     * @brief 从文件描述符(socket)读取数据并存入缓冲区。
     * @param fd 要读取的文件描述符。
     * @param savedErrno [输出参数] 用于保存读取过程中发生的错误码。
     * @return ssize_t 读取到的字节数。-1表示错误, 0表示对端关闭连接。
     */
    ssize_t readFd(int fd, int *savedErrno);

    // 通过fd发送数据
    ssize_t writeFd(int fd,int *savedErrno);

private:
    // 获取缓冲区起始地址
    char *begin()
    {
        return &*buffer_.begin();
    }

    const char *begin() const
    {
        return &*buffer_.begin();
    }

    /**
     * @brief 当可写空间不足时, 用于整理或扩容缓冲区的内部函数。
     * @param len 需要的最小可写空间。
     */
    void makeSpace(size_t len);

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};