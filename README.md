# 说明
	用途: 学习(简单的服务端程序).
	模式: 半同步半异步
	环境: 任意linux64位系统
	编译: 进目录执行 make
	启动: ./simple + 程序目录(脚本目录, 一个脚本目录代表一个程序), 例如 ./simple ./test
	测试: telnet 0.0.0.0 9999
	架构: gate模块(网络收发),service模块(业务处理)
	协议: string + \r\n, 可以根据自己的需求来换(如protobuf, msgpack)
	目录: 3rd 第三方工具,一些外部项目的头文件,一些.so文件,可无视
		  common  底层一些可重用的代码
		  scripts 业务脚本
		  luautil 业务层可重用的一些代码
		  common/timer 基于时间轮的定时器
		  common/somgr 基于epoll的tcp连接管理器 
	