// filename: NaiveServer.cpp
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstdio>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <fcntl.h>
#include <cstdlib>
#include <cassert>
#include <sys/errno.h>

#include "locker.h"
#include "threadpoll.h"
#include "http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);

void addsig(int sig, void(handler)(int), bool restart = true)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;
	if (restart)
		sa.sa_flags |= SA_RESTART;

	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char* info)
{
	printf("%s", info);
	send(connfd, info, strlen(info), 0);
	close(connfd);
}

int main(int argc, char* argv[])
{
	if (argc <= 2)
	{
		printf("usage: %s ip_address port_nubmer\n", basename(argv[0]));
		return 1;
	}
	
	const char* ip = argv[1];
	int port = atoi(argv[2]);

	// 忽略SIGPIPE信号
	// 当客户端close一个连接时，若server端接着发数据。
	// 根据TCP 协议的规定，会收到一个RST响应，
	// 服务器再往client发送数据时，系统会发出一个SIGPIPE信号给进程，告诉进程这个连接已经断开了，不要再写了
	// 根据信号的默认处理规则SIGPIPE信号的默认执行动作是terminate(终止、退出), 所以服务器会退出。
	// 若不想服务器退出可以把SIGPIPE设为SIG_IGN，即忽略SIGPIPE信号
	addsig(SIGPIPE, SIG_IGN);

	// 创建线程池
	threadpool<http_conn>* pool = nullptr;
	try
	{
		pool = new threadpool<http_conn>;
	}
	catch (...)
	{
		return 1;
	}

	// 预先为每个可能的客户连接分配一个http_conn对象
	http_conn* users = new http_conn[MAX_FD];
	assert(users);
	int user_count = 0;

	int listenfd = socket(PF_INET, SOCK_STREAM, 0);
	assert(listenfd >= 0);

	struct linger tmp = { 1, 0 };
	setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

	int ret = 0;
	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &address.sin_addr);
	address.sin_port = htons(port);

	ret = bind(listenfd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address));
	assert(ret >= 0);

	ret = listen(listenfd, 5);
	assert(ret >= 0);

	epoll_event events[MAX_EVENT_NUMBER];
	int epollfd = epoll_create(5);
	assert(epollfd != -1);
	
	addfd(epollfd, listenfd, false);
	http_conn::m_epollfd = epollfd;

	while (true)
	{
		int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if ((number < 0) && (errno != EINTR))
		{
			printf("epoll failure\n");
			break;
		}

		for (int i(0); i < number; ++i)
		{
			int sockfd = events[i].data.fd;
			if (sockfd == listenfd)
			{
				struct sockaddr_in client_address;
				socklen_t client_addrlength = sizeof(client_address);
				int connfd = accept(listenfd, reinterpret_cast<struct sockaddr*>(&client_address),
					&client_addrlength);

				if (connfd < 0)
				{
					printf("errno is: %d\n", errno);
					continue;
				}
				if (http_conn::m_user_count >= MAX_FD)
				{
					show_error(connfd, "Internal server busy");
					continue;
				}
				// 初始化客户连接
				users[connfd].init(connfd, client_address);
			}
			else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
				// 如果有异常，直接关闭客户连接
				users[sockfd].close_conn();
			else if (events[i].events & EPOLLIN)
			{
				// 根据读结果，决定是否将任务添加到线程池，还是关闭连接
				if (users[sockfd].read())
					pool->append(&(users[sockfd]));
				else
					users[sockfd].close_conn();
			}
			else if (events[i].events & EPOLLOUT)
			{
				// 根据写的结果，决定是否关闭连接
				if (!users[sockfd].write())
					users[sockfd].close_conn();
			}
			else {}							
		}
	}
	close(epollfd);
	close(listenfd);
	delete [] users;
	delete pool;
	return 0;
}