# 1. 基本设置
cmake_minimum_required(VERSION 3.10)
project(MyMuduoExample CXX)

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
set(CMAKE_CXX_STANDARD 17)

# 2. 定义我们的可执行目标
#    这个项目很简单, 只有一个 main.cpp, 目标是生成一个叫 echo_server_example 的程序
add_executable(echo_server_example main.cpp)

# 3. 链接依赖库
#    这是与库开发时最大的不同之处
target_link_libraries(echo_server_example PRIVATE 
    mymuduo
    pthread
)