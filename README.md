# 说明
	用途: 学习(简单的服务端程序模型).
	工作模式: 半同步半异步
	环境: 任意linux64位系统
	编译: 进目录执行 make
	目录: 3rd 第三方工具,可以无视
	启动: ./simple
	测试: telnet 0.0.0.0 99999
	架构: gate模块(网络收发),service模块(业务处理)
	协议: string + \r\n, 可以根据自己的需求来换(如protobuf, msgpack)
			common  底层一些可重用的代码
			scripts 业务脚本
			luautil 业务层可重用的一些代码
	