# simple
# 用途: 学习(简单的服务端程序原型).
# 环境: 任意linux64位系统.
# 编译: 进目录执行 make 指令.
# 应用场合: 终端通过tcp与该程序通讯,使用json + \r\n 来交互.
# 内部架构: gate模块(接收请求发送响应), service模块(处理请求,输出响应), 这两个模块通过消息队列交换信息.
