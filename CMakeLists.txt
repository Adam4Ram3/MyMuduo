# 1. 设置 CMake 最低版本和项目名称
cmake_minimum_required(VERSION 3.10) # 建议使用一个更新的版本，如3.10
project(mymuduo CXX) # 明确项目使用的语言是C++

# 2. 使用现代方式设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 开启更多警告，并将警告视为错误（开发阶段强烈推荐）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -Wformat -Wformat-security")
# 3. 将构建产物输出到 build 目录下，保持源码目录干净
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

# 4. 明确列出所有源文件 (最推荐的做法)
add_library(mymuduo SHARED
    Logger.cc
    Timestamp.cc
    Channel.cc
    EventLoop.cc
    InetAddress.cc
    TcpServer.cc
    Poller.cc           # <--- 基类的实现文件（如果构造/析构等函数在.cc中）
    EPollPoller.cc      # <--- 【【最关键】】您很可能漏掉了这一行
    DefaultPoller.cc    # <--- 创建 Poller 的工厂函数实现文件
    CurrentThread.cc
    Thread.cc
    EventLoopThread.cc
    EventLoopThreadPool.cc
    Socket.cc
    Acceptor.cc
    Buffer.cc
    TcpConnection.cc
)

# 5. 为 mymuduo 库链接它所依赖的外部库 (例如线程库)
#    注意：这里假设你的库代码里用到了 pthread
target_link_libraries(mymuduo PRIVATE pthread)


# 6. (可选，但强烈推荐) 添加一个测试程序来使用你的库
#    假设你在 test/main.cpp 创建了一个测试文件
add_executable(my_test_server test/main.cpp)

# 7. 将测试程序与你的库链接起来
target_link_libraries(my_test_server PRIVATE mymuduo)