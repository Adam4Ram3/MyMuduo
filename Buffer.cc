#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h> // for readv
#include <unistd.h>  // for read

/**
 * @brief 从文件描述符(socket)读取数据并存入缓冲区。
 * @details
 * 这是一个非常重要的函数, 它使用 readv 系统调用进行"分散读", 
 * 这样做可以一次性将数据读入到两个缓冲区: Buffer内部的可写空间和栈上
 * 的一个临时空间。这可以避免一次读取的数据过多而导致Buffer频繁扩容, 
 * 同时也能一次性读取尽可能多的数据, 减少read系统调用的次数, 提升性能。
 * * @param fd 要读取的文件描述符。
 * @param savedErrno [输出参数] 用于保存读取过程中发生的错误码。
 * @return ssize_t 读取到的字节数。-1表示错误, 0表示对端关闭连接。
 */
ssize_t Buffer::readFd(int fd, int *savedErrno)
{
    // 在栈上创建一个临时的额外缓冲区
    char extraBuf[65536]; // 64K
    
    struct iovec vec[2];
    const size_t writable = writableBytes();
    
    // 第一块缓冲区指向 Buffer 内部的可写空间
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;
    // 第二块缓冲区指向栈上的临时空间
    vec[1].iov_base = extraBuf;
    vec[1].iov_len = sizeof(extraBuf);

    // 当 Buffer 的可写空间足够大时, 一次 readv 就能读完所有数据,
    // 如果不够, 就会读到栈上的 extraBuf 中, 然后再 append 到 Buffer 中。
    const int iovcnt = (writable < sizeof(extraBuf)) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);

    if (n < 0)
    {
        *savedErrno = errno;
    }
    else if (static_cast<size_t>(n) <= writable)
    {
        // 读取的数据只占用了 Buffer 的可写空间
        writerIndex_ += n;
    }
    else
    {
        // Buffer 的可写空间已全部用完, 并且数据还写入了 extraBuf
        writerIndex_ = buffer_.size();
        // 将 extraBuf 中的数据追加到 Buffer 中 (这会触发 makeSpace 和 vector 的扩容)
        append(extraBuf, n - writable);
    }
    return n;
}


/**
 * @brief 当可写空间不足时, 用于整理或扩容缓冲区的内部函数。
 * @details
 * 这是一个核心的性能优化。它首先检查 "已读空间 + 可写空间" 是否足够,
 * 如果足够, 就将可读数据前移, 复用已读空间, 避免内存分配。
 * 如果总空间仍然不足, 才进行 vector 的扩容。
 * @param len 需要的最小可写空间。
 */
void Buffer::makeSpace(size_t len)
{
    // 如果 "已读空间 + 可写空间" < "需要的空间 + 预留空间", 说明总空间不足, 必须扩容
    if (writableBytes() + prependableBytes() < len + kCheapPrepend)
    {
        buffer_.resize(writerIndex_ + len);
    }
    else // 总空间足够, 只是需要整理
    {
        size_t readable = readableBytes();
        // 使用 std::copy 将可读数据块 [readerIndex_, writerIndex_) 移动到缓冲区的前端
        std::copy(begin() + readerIndex_,
                  begin() + writerIndex_,
                  begin() + kCheapPrepend);
        // 更新读写指针
        readerIndex_ = kCheapPrepend;
        writerIndex_ = readerIndex_ + readable;
    }
}