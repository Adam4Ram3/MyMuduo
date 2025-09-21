# 1. 设置 CMake 最低版本和项目名称
cmake_minimum_required(VERSION 3.10) # 建议使用一个更新的版本，如3.10
project(mymuduo CXX) # 明确项目使用的语言是C++
# ====================================================================
# ==                      【在这里添加新代码】                        ==
# ====================================================================
# 强制设定构建类型为 Debug, 这会应用下面的 CMAKE_CXX_FLAGS_DEBUG
set(CMAKE_BUILD_TYPE Debug)

# 为 Debug 模式设置特定的编译选项
# -O0:  告诉编译器“关闭所有优化” (O后面的数字是0)
# -g3:  生成最详细的调试信息
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g3")
# ====================================================================
# 2. 使用现代方式设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 开启更多警告，并将警告视为错误（开发阶段强烈推荐）
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -Wformat -Wformat-security")
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
# ==========================================================
# ==                  【在这里添加新代码】                  ==
# ==========================================================
# 告诉其他目标，如果想使用 mymuduo 库，应该到哪里去找它的头文件
target_include_directories(mymuduo PUBLIC 
    ${PROJECT_SOURCE_DIR}
)
# ==========================================================


# 6. (可选，但强烈推荐) 添加一个测试程序来使用你的库
#    假设你在 test/main.cpp 创建了一个测试文件
add_executable(my_test_server test/main.cpp)

# 7. 将测试程序与你的库链接起来
target_link_libraries(my_test_server PRIVATE mymuduo)