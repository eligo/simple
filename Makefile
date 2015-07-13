#CXX=g++
#CC=gcc
INCLUDES=-I/usr/include\
		 -I/usr/local/include\
		 -I./3rd/include\
		 -I./3rd/include/lua-5.14

CXXFLAGS=-c -Wall -O2 -g $(INCLUDES)

LIBS=./3rd/lib/liblua.a
	
SRCDIRS=. ./common ./common/somgr ./common/timer
SRCS=$(foreach dir,$(SRCDIRS),$(wildcard $(dir)/*.c))

OBJS=$(SRCS:.c=.o)
PROG=./simple

all: $(PROG) $(MODULE)

install:
clean:
	rm -rf $(OBJS) $(PROG) $(PROG).core

$(PROG): $(OBJS)
	gcc -g $(OBJS) -o $(PROG) $(LIBS) -lpthread -ldl -lrt -lc -rdynamic -lpthread -lm
.c.o:
	gcc $(CXXFLAGS) -fPIC $< -o $@
	
#程序(simple)只需要依赖以下几个动态库, 基本上linux系统都会有的(lsof -p pid 可查看, readelf -d ./eligo 可查看)
# Tag        Type                         Name/Value
#0x0000000000000001 (NEEDED)             Shared library: [libpthread.so.0]
#0x0000000000000001 (NEEDED)             Shared library: [libdl.so.2]
#0x0000000000000001 (NEEDED)             Shared library: [librt.so.1]
#0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
#0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]
#0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
#0x0000000000000001 (NEEDED)             Shared library: [ld-linux-x86-64.so.2]
#0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]

