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
 * @brief 将缓冲区中当前可读的数据一次性写入到文件描述符 (socket)。
 * @details
 * 写操作相对简单：直接把 [readerIndex_, writerIndex_) 这一段连续内存
 * 通过 ::write 发出去。
 * 特点与说明：
 * 1. 只调用一次 write，未使用 writev，因为发送区本身就是连续的。
 * 2. 若写入成功，返回写出的字节数；这里不主动前移 readerIndex_，
 *    上层（如 TcpConnection::handleWrite）根据返回值决定调用 retrieve(n)。
 * 3. 若返回 -1，表示写失败（例如 EAGAIN / EWOULDBLOCK），错误码通过 savedErrno 传出。
 * 4. 若写满对端发送缓冲区，会出现 EAGAIN，此时应等待下次 EPOLLOUT 事件再继续发送。
 *
 * @param fd 目标文件描述符（通常是 socket）。
 * @param savedErrno [输出] 若写失败，保存 errno 供上层决策（是否重试或关闭）。
 * @return ssize_t 写入的字节数；-1 表示出错；0 很少出现（通常表示对端异常）。
 */
ssize_t Buffer::writeFd(int fd, int *savedErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0)
    {
        *savedErrno = errno;
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