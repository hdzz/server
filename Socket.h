//--------------------------------------------------------------------
// 文件名:		LinuxSocket.h
// 内  容:		
// 说  明:		
//--------------------------------------------------------------------

#ifndef _SYSTEM_LINUXSOCKET_H
#define _SYSTEM_LINUXSOCKET_H

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <net/if.h> 
#include <net/if_arp.h> 
#include <sys/param.h> 
#include <sys/ioctl.h> 
#define MAXINTERFACES 16 /* 最大接口数 */

#include <string>


#define INVALID_SOCKET -1

typedef int socket_t;

// 初始化通讯
inline bool CommSystemStartup()
{
	return true;
}

// 结束通讯
inline bool CommSystemCleanup()
{
	return true;
}

// 创建TCP连接
inline bool SocketOpenTCP(socket_t* pSockHandle)
{
	socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
	
	if (-1 == sock)
	{
		return false;
	}
	
	*pSockHandle = sock;
	
	return true;
}

// 创建UDP连接
inline bool SocketOpenUDP(socket_t* pSockHandle)
{
	socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
	
	if (-1 == sock)
	{
		return false;
	}
	
	*pSockHandle = sock;
	
	return true;
}

// 关闭连接
inline bool SocketClose(socket_t sock_handle)
{
	int res = close(sock_handle);
	
	if (-1 == res)
	{
		return false;
	}
	
	return true;
}

// 停止收发
inline bool SocketShutdown(socket_t sock_handle)
{
	return shutdown(sock_handle, SHUT_RDWR) == 0;
}

// 停止发送
inline bool SocketShutdownSend(socket_t sock_handle)
{
	return shutdown(sock_handle, SHUT_WR) == 0;
}

// 连接
inline bool SocketConnect(socket_t sock_handle, const char* addr, 
	int port)
{
	sockaddr_in sa;	
	
	memset(&sa, 0, sizeof(sa));	
	
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr(addr);
	sa.sin_port = htons(port);
	
	int res = connect(sock_handle, (sockaddr*)&sa, sizeof(sockaddr));
	
	if (-1 == res)
	{
		if (errno != EINPROGRESS)
		{
			return false;
		}
	}
	
	return true;
}

// 绑定端口
inline bool SocketBind(socket_t sock_handle, const char* addr, 
	int port)
{
	sockaddr_in sa;
	
	memset(&sa, 0, sizeof(sa));

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr(addr);
	sa.sin_port = htons((u_short)port);

	int res = ::bind(sock_handle, (const sockaddr*)&sa, sizeof(sa));

	if (-1 == res)
	{
		return false;
	}
			
	return true;
}

// 侦听端口
inline bool SocketListen(socket_t sock_handle, int backlog)
{
	int res = listen(sock_handle, backlog);

	if (-1 == res)
	{
		return false;
	}
	
	return true;
}

// 接受连接
inline bool SocketAccept(socket_t sock_handle, 
	socket_t* pNewSocket, char* remote_addr, 
	size_t remote_addr_size, int* remote_port)
{
	sockaddr_in	sa;
	socklen_t len = sizeof(sockaddr);
	
	int sock = accept(sock_handle, (sockaddr*)&sa, &len);

	if (-1 == sock)
	{
		return false;
	}
	
	*pNewSocket = sock;
	
	if (remote_addr)
	{
		char* addr = inet_ntoa(sa.sin_addr);
		size_t addr_size = strlen(addr);
		
		if (addr_size >= remote_addr_size)
		{
			memcpy(remote_addr, addr, remote_addr_size - 1);
			remote_addr[remote_addr_size - 1] = 0;
		}
		else
		{
			memcpy(remote_addr, addr, addr_size + 1);
		}
	}
	
	if (remote_port)
	{
		*remote_port = sa.sin_port;
	}	
	
	return true;
}

// 获得连接的本地地址
inline bool SocketGetSockName(socket_t sock_handle, char* addr, 
	size_t addr_size, int* port)
{
	sockaddr_in	sa;
	socklen_t len = sizeof(sockaddr);

	int res = getsockname(sock_handle, (sockaddr*)&sa, &len);

	if (-1 == res)
	{
		return false;
	}

	if (addr)
	{
		char* s = inet_ntoa(sa.sin_addr);
		size_t size = strlen(s);

		if (size >= addr_size)
		{
			memcpy(addr, s, addr_size - 1);
			addr[addr_size - 1] = 0;
		}
		else
		{
			memcpy(addr, s, size + 1);
		}
	}

	if (port)
	{
		*port = ntohs(sa.sin_port);
	}	

	return true;
}

// 获得连接的对端地址
inline bool SocketGetPeerName(socket_t sock_handle, char* addr, 
	size_t addr_size, int* port)
{
	sockaddr_in	sa;
	socklen_t len = sizeof(sockaddr);

	int res = getpeername(sock_handle, (sockaddr*)&sa, &len);

	if (-1 == res)
	{
		return false;
	}

	if (addr)
	{
		char* s = inet_ntoa(sa.sin_addr);
		size_t size = strlen(s);

		if (size >= addr_size)
		{
			memcpy(addr, s, addr_size - 1);
			addr[addr_size - 1] = 0;
		}
		else
		{
			memcpy(addr, s, size + 1);
		}
	}

	if (port)
	{
		*port = ntohs(sa.sin_port);
	}	

	return true;
}

// 发送消息
inline int SocketSend(socket_t sock_handle, const char* buffer, 
	size_t size)
{
	ssize_t res = send(sock_handle, buffer, size, 0);
	
	if (-1 == res)
	{
		return -1;
	}
	
	return (int)res;
}

// 异步发送消息
inline int SocketSendAsync(socket_t sock_handle, const char* buffer, 
	size_t size, bool* would_block)
{
	ssize_t res = send(sock_handle, buffer, size, 0);
	
	if (-1 == res)
	{
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
		{
			*would_block = true;
		}
		else
		{
			*would_block = false;
		}
		
		return -1;
	}
	else
	{
		*would_block = false;
	}
	
	return (int)res;
}

// 接收消息
inline bool SocketReceive(socket_t sock_handle, char* buffer,
	size_t size, size_t* pReadSize)
{
	ssize_t res = recv(sock_handle, buffer, size, 0);
	
	if (-1 == res)
	{
		return false;
	}
	
	*pReadSize = res;
	
	return true;
}

// 发送消息到指定地址
inline int SocketSendTo(socket_t sock_handle, const char* buffer, 
	size_t size, const char* addr, int port)
{
	sockaddr_in	sa;
	
	memset(&sa, 0, sizeof(sa));
	
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr(addr);
	sa.sin_port = htons(port);
	
	int res = sendto(sock_handle, buffer, (int)size, 0, (sockaddr*)&sa, 
		sizeof(sockaddr));
	
	if (-1 == res)
	{
		return -1;
	}
	
	return (int)res;
}
	
// 接收消息和地址
inline bool SocketReceiveFrom(socket_t sock_handle, char* buffer,
	size_t size, char* remote_addr, size_t remote_addr_size, int* remote_port,
	size_t* pReadSize)
{
	sockaddr_in sa;
	socklen_t len = sizeof(sockaddr);

	int res = recvfrom(sock_handle, buffer, (int)size, 0, (sockaddr*)&sa, 
		&len);
	
	if (-1 == res)
	{
		return false;
	}

	*pReadSize = res;

	if (remote_addr)
	{
		char* addr = inet_ntoa(sa.sin_addr);
		size_t addr_size = strlen(addr);
		
		if (addr_size >= remote_addr_size)
		{
			memcpy(remote_addr, addr, remote_addr_size - 1);
			remote_addr[remote_addr_size - 1] = 0;
		}
		else
		{
			memcpy(remote_addr, addr, addr_size + 1);
		}
	}
	
	if (remote_port)
	{
		*remote_port = ntohs(sa.sin_port);
	}
	
	return true;
}

// 监视读取事件
inline bool SocketSelectRead(socket_t sock_handle, int wait_ms, 
	bool* pReadFlag, bool* pExceptFlag)
{
	// linux下的select的fd超过1024会有问题，所以必须用poll
	struct pollfd pfd;
	
	pfd.fd = sock_handle;
	pfd.events = POLLIN;
	pfd.revents = 0;
	
	int res = poll(&pfd, 1, wait_ms);
	
	if (-1 == res)
	{
		return false;
	}
	
	*pReadFlag = false;
	*pExceptFlag = false;

	if (res > 0)
	{
		if (pfd.revents & POLLIN)
		{
			*pReadFlag = true;
		}
		
		if (pfd.revents & (POLLHUP | POLLERR))
		{
			*pExceptFlag = true;
		}
	}
	
	return true;
}

// 监视事件
inline bool SocketSelect(socket_t sock_handle, int wait_ms, 
	bool* pReadFlag, bool* pWriteFlag, bool* pExceptFlag)
{
	struct pollfd pfd;
	
	pfd.fd = sock_handle;
	pfd.events = POLLIN | POLLOUT;
	pfd.revents = 0;
	
	int res = poll(&pfd, 1, wait_ms);
	
	if (-1 == res)
	{
		return false;
	}
	
	*pReadFlag = false;
	*pWriteFlag = false;
	*pExceptFlag = false;

	if (res > 0)
	{
		if (pfd.revents & POLLIN)
		{
			*pReadFlag = true;
		}
		
		if (pfd.revents & POLLOUT)
		{
			*pWriteFlag = true;
		}
		
		if (pfd.revents & (POLLHUP | POLLERR))
		{
			*pExceptFlag = true;
		}
	}
	
	return true;
}

// 设置为非阻塞模式
inline bool SocketSetNonBlocking(socket_t sock_handle)
{
	int flags = fcntl(sock_handle, F_GETFL);
	
	if (-1 == flags)
	{
		return false;
	}
	
	if (fcntl(sock_handle, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		return false;
	}
	
	return true;
}

// 设置为阻塞模式
inline bool SocketSetBlocking(socket_t sock_handle)
{
	int flags = fcntl(sock_handle, F_GETFL);
	
	if (-1 == flags)
	{
		return false;
	}
	
	if (fcntl(sock_handle, F_SETFL, flags & ~O_NONBLOCK) == -1)
	{
		return false;
	}
	
	return true;
}

// 设置为可重复使用地址
inline bool SocketSetReuseAddr(socket_t sock_handle)
{
	int flag = 1;

	int res = setsockopt(sock_handle, SOL_SOCKET, SO_REUSEADDR, 
		(const char*)&flag, sizeof(flag));
		
	if (-1 == res)
	{
		return false;
	}
	
	return true;
}

// 设置为可广播
inline bool SocketSetBroadcast(socket_t sock_handle)
{
	int flag = 1;

	int res = setsockopt(sock_handle, SOL_SOCKET, SO_BROADCAST, 
		(const char*)&flag, sizeof(flag));
		
	if (-1 == res)
	{
		return false;
	}
	
	return true;
}

// 设置为立即发送
inline bool SocketSetNoDelay(socket_t sock_handle)
{
	int flag = 1;

	int res = setsockopt(sock_handle, IPPROTO_TCP, TCP_NODELAY, 
		(const char*)&flag, sizeof(flag));
		
	if (-1 == res)
	{
		return false;
	}
	
	return true;
}

// 获得在广播模式下应该绑定的地址
inline const char* GetBroadcastBindAddr(const char* local_addr, 
	const char* broad_addr)
{
	return broad_addr;
}

// 获得错误信息
inline const char* SocketGetError(char* buffer, size_t size)
{
	return strerror_r(errno, buffer, size);
}

inline std::string GetBroadcastAddr(const char *ip)
{
	register int fd, intrface, retn = 0; 

	struct ifreq buf[MAXINTERFACES]; /* ifreq结构数组 */
	struct arpreq arp; 
	struct ifconf ifc; 

	if ( (fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) 
	{
		return "";
	}

	ifc.ifc_len = sizeof(buf); 
	ifc.ifc_buf = (caddr_t) buf; 
	if ( ioctl(fd, SIOCGIFCONF, (char *)&ifc) != 0 ) 
	{
		close(fd);
		return "";
	}


	//获取接口数量信息

	intrface = ifc.ifc_len / sizeof (struct ifreq); 
	//printf("interface num is intrface=%d\n",intrface); 
	//puts("");

	//根据借口信息循环获取设备IP和MAC地址
	while ( (intrface--) > 0) 
	{ 
		////获取设备名称
		//printf ("net device %s\n", buf[intrface].ifr_name); 

		//判断网卡类型 
		if ( (ioctl(fd, SIOCGIFFLAGS, (char *)&buf[intrface])) != 0 ) 
		{ 
			continue;
		} 

		//判断网卡状态 
		if ( (buf[intrface].ifr_flags & IFF_UP) == 0 ) 
		{ 
			continue;
		} 

		//获取当前网卡的IP地址 
		if ( (ioctl (fd, SIOCGIFADDR, (char *) &buf[intrface])) != 0 ) 
		{ 
			continue;
		} 

		if ( ((struct sockaddr_in*)(&buf[intrface].ifr_addr))->sin_addr.s_addr != inet_addr(ip) )
		{
			continue;
		}

		//printf("IP address is:"); 
		//puts((char *)inet_ntoa(((struct sockaddr_in*)(&buf[intrface].ifr_addr))->sin_addr)); 
		////printf("\n%d\n"buf[intrface].ifr_addr))->sin_addr.s_addr); 
		////puts (buf[intrface].ifr_addr.sa_data); 

		///* this section can't get Hardware Address,I don't know whether the reason is module driver*/ 


		//广播地址
		if ( (ioctl(fd, SIOCGIFBRDADDR, (char *) &buf[intrface])) != 0 )
		{
			close (fd); 
			return "";
		}

		close (fd); 
		return (char*)inet_ntoa(((struct sockaddr_in*) (&buf[intrface].ifr_addr))->sin_addr);
	} //while

	close (fd); 

	return ""; 
}

#endif // _SYSTEM_LINUXSOCKET_H

