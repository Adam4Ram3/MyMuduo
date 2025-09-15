// file: /home/tbce/MyMuduo/test/main.cpp
#include <iostream>
#include "../Logger.h" // 假设 Logger.h 在源码根目录

int main() 
{
    // 设置日志级别，这样 INFO 级别的信息才能被看到
    Logger::instance().setLogLevel(INFO);

    LOG_INFO("Hello from my_test_server!");
    LOG_ERROR("This is an error message.");
    LOG_INFO("func=%s => fd=%d events=%d index=%d \n", __FUNCTION__, 1, 2, 1);
    std::cout << "MyMuduo test server is running." << std::endl;

    return 0;
}