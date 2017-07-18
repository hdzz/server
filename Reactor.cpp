#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cstdio>
#include "Socket.h"
#include "Reactor.h"
// 性能计时
double g_dEventPerfCounter = 0.0;
double g_dTimerPerfCounter = 0.0;

//#define EPOLLRDHUP 0x2000

// 每次检测的事件数量
#define INIT_EVENTS_PER_LOOP 32
#define MAX_EVENTS_PER_LOOP 8192*2

#define TYPE_CONNECTOR 0
#define	TYPE_BROADCAST 1
#define TYPE_LISTENER 2
#define MAKE_EV_DATA(type, index, sock) \
	((UINT64((type << 24) + index) << 32) + UINT64(sock))

// Reactor

struct CReactor::broadcast_t
{
	socket_t Socket;
	char strLocalAddr[32];
	char strBroadAddr[32];
	int nPort;
	UINT32 nInBufferLen;
	char* pInBuf;
	broadcast_callback BroadcastCallback;
	void* pContext;
};

struct CReactor::listener_t
{
	socket_t Socket;
	char strAddr[32];
	int nPort;
	UINT32 nInBufferLen;
	UINT32 nOutBufferLen;
	UINT32 nOutBufferMax;
	accept_callback AcceptCallback;
	close_callback CloseCallback;
	receive_callback ReceiveCallback;
	void* pContext;
};

struct CReactor::udp_t
{
	socket_t socket;
	char strAddr[32];
	int nPort;
};

struct CReactor::connector_t
{
	socket_t Socket;
	UINT64 nEvData;
	char strAddr[32];
	int nPort;
	UINT32 nInBufferLen;
	UINT32 nOutBufferLen;
	UINT32 nOutBufferMax;
	connect_callback ConnectCallback;
	close_callback CloseCallback;
	receive_callback ReceiveCallback;
	void* pContext;
	int nTimeout;
	struct timeval tvTimeout;
	float fCounter;
	int nSendEmpty;
	char* pOutBuf;
	char* pSendBegin;
	UINT32 nSendRemain;
	UINT32 nSendOffset;
	char* pInBuf;
};

struct CReactor::timer_t
{
	float fSeconds;
	float fCounter;
	timer_callback TimerCallback;
	void* pContext;
};

CReactor::CReactor()
{
	m_nEventsPerLoop = INIT_EVENTS_PER_LOOP;
	m_pEvents = NEW struct epoll_event[m_nEventsPerLoop];
	//ASSERT_RETURN(m_pEvents);
	m_Epoll = -1;
	m_fMinTime = 1.0f;
	//m_pTimer = NEW CTickTimer;
	//ASSERT_RETURN(m_pTimer);
}

CReactor::~CReactor()
{
	Stop();
	//delete m_pTimer;
	//m_pTimer = NULL;

	delete[] m_pEvents;
	m_pEvents = NULL;

	delete m_pUdp;
	m_pUdp = NULL;
}

bool CReactor::Start()
{
	//m_pTimer->Initialize();
	
	// 创建EPOLL
	m_Epoll = epoll_create(32000);
	
	if (-1 == m_Epoll)
	{
		std::cout << "(CReactor::Start)epoll_create failed" << std::endl;
		return false;
	}
	
	return true;
}

bool CReactor::Stop()
{
	for (size_t k1 = 0; k1 < m_Broadcasts.size(); ++k1)
	{
		if (m_Broadcasts[k1])
		{
			DeleteBroadcast((int)k1);
		}
	}

	for (size_t k2 = 0; k2 < m_Listeners.size(); ++k2)
	{
		if (m_Listeners[k2])
		{
			DeleteListener((int)k2);
		}
	}

	for (size_t k3 = 0; k3 < m_Connectors.size(); ++k3)
	{
		if (m_Connectors[k3])
		{
			DeleteConnector((int)k3);
		}
	}

	/*for (size_t k4 = 0; k4 < m_Timers.size(); ++k4)
	{
		if (m_Timers[k4])
		{
			DeleteTimer((int)k4);
		}
	}*/

	if (m_Epoll != -1)
	{
		close(m_Epoll);
		m_Epoll = -1;
	}

	return true;
}

int CReactor::CreateBroadcast(const char* local_addr, const char* broad_addr,
	int port, size_t in_buf_len, broadcast_callback cb, void* context)
{
	ASSERT_RETURN_VALUE(local_addr != NULL, -1);
	ASSERT_RETURN_VALUE(broad_addr != NULL, -1);
	ASSERT_RETURN_VALUE(port > 0, -1);

	socket_t sock;
	
	if (!SocketOpenUDP(&sock))
	{
		//std::cout << ("(CReactor::CreateBroadcast)no socket resource");
		return -1;
	}

	if (!SocketSetNonBlocking(sock))
	{
		char info[128];
		//std::cout << ("(CReactor::CreateBroadcast)set non blocking failed");
		//std::cout << ( SocketGetError(info, sizeof(info)));
		SocketClose(sock);
		return -1;
	}

	if (!SocketSetReuseAddr(sock))
	{
		char info[128];
		//std::cout << ("(CReactor::CreateBroadcast)set reuse address failed");
		//std::cout << ( SocketGetError(info, sizeof(info)));
		SocketClose(sock);
		return -1;
	}

	if (!SocketSetBroadcast(sock))
	{
		char info[128];
		//std::cout << ("(CReactor::CreateBroadcast)set broadcast failed");
		//std::cout << ( SocketGetError(info, sizeof(info)));
		 SocketClose(sock);
		return -1;
	}

	// 在不同的操作系统下应该绑定的地址不一样
	const char* bind_addr =  GetBroadcastBindAddr(local_addr, broad_addr);

	if (!SocketBind(sock, bind_addr, port))
	{
		char info[128];
		//std::cout << ("(CReactor::CreateBroadcast)bind failed");
		//std::cout << ( SocketGetError(info, sizeof(info)));
		 SocketClose(sock);
		return -1;
	}

	broadcast_t* pBroad = NEW broadcast_t;
	ASSERT_RETURN_VALUE(pBroad, -1);
	
	memset(pBroad, 0, sizeof(broadcast_t));
	pBroad->Socket = sock;
	strncpy(pBroad->strLocalAddr , local_addr, sizeof(pBroad->strLocalAddr));
	strncpy(pBroad->strBroadAddr, broad_addr, sizeof(pBroad->strBroadAddr));
	pBroad->nPort = port;
	pBroad->nInBufferLen = in_buf_len;
	//pBroad->pInBuf = NEW char[in_buf_len];
	ASSERT_RETURN_VALUE(pBroad->pInBuf, -1);
	pBroad->BroadcastCallback = cb;
	pBroad->pContext = context;

	size_t index;
	
	if (m_BroadcastFrees.empty())
	{
		index = m_Broadcasts.size();
		m_Broadcasts.push_back(pBroad);
	}
	else
	{
		index = m_BroadcastFrees.back();
		m_BroadcastFrees.pop_back();
		m_Broadcasts[index] = pBroad;
	}

	// 添加到EPOLL
	struct epoll_event ev;
	
	ev.data.u64 = MAKE_EV_DATA(TYPE_BROADCAST, index, sock);
	ev.events = EPOLLIN | EPOLLRDHUP;

	if (epoll_ctl(m_Epoll, EPOLL_CTL_ADD, sock, &ev) != 0)
	{
		//std::cout << ("(CReactor::CreateBroadcast)epoll add failed");
		DeleteBroadcast((int)index);
		return -1;
	}

	return (int)index;
}

bool CReactor::DeleteBroadcast(int broadcast_id)
{
	if (size_t(broadcast_id) >= m_Broadcasts.size())
	{
		return false;
	}
	ASSERT_RETURN_VALUE(size_t(broadcast_id) < m_Broadcasts.size(), false);
	
	broadcast_t* pBroad = m_Broadcasts[broadcast_id];

	//ASSERT_RETURN_VALUE(pBroad != NULL, false);

	if (pBroad->Socket != PORT_INVALID_SOCKET)
	{
		// 从EPOLL移除
		struct epoll_event ev;
		
		ev.data.fd = pBroad->Socket;
		ev.events = 0;
		
		if (epoll_ctl(m_Epoll, EPOLL_CTL_DEL, pBroad->Socket, &ev) != 0)
		{
			std::cout << ("(CReactor::DeleteBroadcast)epoll remove failed");
		}

		 SocketClose(pBroad->Socket);
	}

	delete[] pBroad->pInBuf;
	delete pBroad;
	m_Broadcasts[broadcast_id] = NULL;
	m_BroadcastFrees.push_back(broadcast_id);

	return true;
}

bool CReactor::SendBroadcast(int broadcast_id, const void* pdata, size_t len)
{
	if (size_t(broadcast_id) >= m_Broadcasts.size())
	{
		return false;
	}
	if (!pdata)
	{
		return false;
	}

	broadcast_t* pBroad = m_Broadcasts[broadcast_id];

	if (!pBroad)
	{
		return false;
	}

	int res =  SocketSendTo(pBroad->Socket, (const char*)pdata, len, 
		pBroad->strBroadAddr, pBroad->nPort);

	if (res < 0)
	{
		char info[128];
		std::cout << "(CReactor::SendBroadcast)send data failed" << std::endl;
		//std::cout << ( SocketGetError(info, sizeof(info)));
		return false;
	}

	return true;
}


bool CReactor::InitUdp(const char* addr, int port)
{
	socket_t sock;

	if (!SocketOpenUDP(&sock))
	{
		std::cout << "no socket resource" << std::endl;;
		return false;
	}

	if (!SocketSetNonBlocking(sock))
	{
		char info[128];
		std::cout << "set non blocking failed" << std::endl;
		//std::cerr << ( SocketGetError(info, sizeof(info)));
		 SocketClose(sock);
		return false;
	}

	if (!SocketSetReuseAddr(sock))
	{
		char info[128];
		std::cout << ("set reuse address failed") << std::endl;
		std::cout << ( SocketGetError(info, sizeof(info))) << std::endl;
		 SocketClose(sock);
		return false;
	}

	if (!SocketSetBroadcast(sock))
	{
		char info[128];
		std::cout << ("set broadcast failed");
		std::cout << ( SocketGetError(info, sizeof(info)));
		 SocketClose(sock);
		return false;
	}

	m_pUdp = NEW udp_t;
	//ASSERT_RETURN_VALUE(m_pUdp, false);
	m_pUdp->socket = sock;
	strncpy(m_pUdp->strAddr, addr, sizeof(m_pUdp->strAddr));
	m_pUdp->nPort = port;

	return true;
}

bool CReactor::SendUdp(const void* pdata, size_t len)
{
	int res =  SocketSendTo(m_pUdp->socket, (const char*)pdata, len, 
		m_pUdp->strAddr, m_pUdp->nPort);

	if (res < 0)
	{
		char info[128];
		std::cerr << ("send data failed");
		std::cerr << ( SocketGetError(info, sizeof(info)));
		return false;
	}

	return true;
}

bool CReactor::DeleteUdp()
{
	if (m_pUdp->socket != PORT_INVALID_SOCKET)
	{
		 SocketClose(m_pUdp->socket);
	}

	delete m_pUdp;
	m_pUdp = NULL;

	return true;
}

int CReactor::CreateListener(const char* addr, int port, int backlog, 
	size_t in_buf_len, size_t out_buf_len, size_t out_buf_max, 
	accept_callback accept_cb, close_callback close_cb, 
	receive_callback recv_cb, void* context, size_t)
{
	if (addr == NULL || port < 0)
	{
		return -1;
	}
	
	socket_t sock;
	
	if (!SocketOpenTCP(&sock))
	{
		std::cout << "(CReactor::CreateListener)no resource" << std::endl;
		return -1;
	}

	if (!SocketSetNonBlocking(sock))
	{
		char info[128];
		std::cout << ("(CReactor::CreateListener)set non blocking failed") << std::endl;
		std::cout << ( SocketGetError(info, sizeof(info))) << std::endl;
		 SocketClose(sock);
		return -1;
	}

	if (!SocketSetReuseAddr(sock))
	{
		char info[128];
		std::cout << ("(CReactor::CreateListener)set reuse address failed") << std::endl;
		std::cout << ( SocketGetError(info, sizeof(info))) << std::endl;
		 SocketClose(sock);
		return -1;
	}

	// 绑定地址
	if (!SocketBind(sock, addr, port))
	{
		char info[128];
		std::cout << "(CReactor::CreateListener)bind error! " << addr << " " << port << std::endl;
		//std::cout << (info);
		//std::cout << ( SocketGetError(info, sizeof(info)));
		 SocketClose(sock);
		return -1;
	}

	if (!SocketListen(sock, backlog))
	{
		char info[128];
		std::cout << ("(CReactor::CreateListener)listen error") << std::endl;
		std::cout << ( SocketGetError(info, sizeof(info))) << std::endl;
		 SocketClose(sock);
		return -1;
	}

	listener_t* pListener = NEW listener_t;
	//ASSERT_RETURN_VALUE(pListener, -1);

	memset(pListener, 0, sizeof(listener_t));
	pListener->Socket = sock;
	strncpy(pListener->strAddr, addr, sizeof(pListener->strAddr));
	pListener->nPort = port;
	pListener->nInBufferLen = in_buf_len;
	pListener->nOutBufferLen = out_buf_len;
	pListener->nOutBufferMax = out_buf_max;
	pListener->AcceptCallback = accept_cb;
	pListener->CloseCallback = close_cb;
	pListener->ReceiveCallback = recv_cb;
	pListener->pContext = context;

	size_t index;

	if (m_ListenerFrees.empty())
	{
		index = m_Listeners.size();
		m_Listeners.push_back(pListener);
	}
	else
	{
		index = m_ListenerFrees.back();
		m_ListenerFrees.pop_back();
		m_Listeners[index] = pListener;
	}

	// 添加到EPOLL
	struct epoll_event ev;
	
	ev.data.u64 = MAKE_EV_DATA(TYPE_LISTENER, index, sock);
	ev.events = EPOLLIN;

	if (epoll_ctl(m_Epoll, EPOLL_CTL_ADD, sock, &ev) != 0)
	{
		std::cout << ("(CReactor::CreateListener)epoll add failed");
		DeleteListener((int)index);
		return -1;
	}

	return (int)index;
}

bool CReactor::DeleteListener(int listener_id)
{
	if (size_t(listener_id) >= m_Listeners.size())
	{
		return false;
	}
	listener_t* pListener = m_Listeners[listener_id];

	if(!pListener)
	{
		return false;
	}
	if (pListener->Socket != PORT_INVALID_SOCKET)
	{
		// 从EPOLL移除
		struct epoll_event ev;
		
		ev.data.fd = pListener->Socket;
		ev.events = 0;
		
		if (epoll_ctl(m_Epoll, EPOLL_CTL_DEL, pListener->Socket, &ev) != 0)
		{
			std::cout << ("(CReactor::DeleteListener)epoll remove failed") << std::endl;
		}

		 SocketClose(pListener->Socket);
	}

	delete pListener;
	m_Listeners[listener_id] = NULL;
	m_ListenerFrees.push_back(listener_id);

	return true;
}

size_t CReactor::GetListenerSock(int listener_id)
{
	if (size_t(listener_id) >= m_Listeners.size())
	{
		return PORT_INVALID_SOCKET;
	}

	listener_t* pListener = m_Listeners[listener_id];

	if (NULL == pListener)
	{
		return PORT_INVALID_SOCKET;
	}

	return (size_t)pListener->Socket;
}

int CReactor::GetListenerPort(int listener_id)
{
	if (size_t(listener_id) >= m_Listeners.size())
	{
		return -1;
	}

	listener_t* pListener = m_Listeners[listener_id];

	if (NULL == pListener)
	{
		return -1;
	}

	sockaddr_in sa;
	socklen_t len = sizeof(sockaddr);

	int res = getsockname(pListener->Socket, (sockaddr*)&sa, &len);

	if (-1 == res)
	{
		return -1;
	}

	return ntohs(sa.sin_port);
}

int CReactor::CreateConnector(const char* addr, int port, size_t in_buf_len, 
	size_t out_buf_len, size_t out_buf_max, int timeout, 
	connect_callback conn_cb,  close_callback close_cb, 
	receive_callback recv_cb, void* context)
{
	ASSERT_RETURN_VALUE(addr != NULL, -1);
	ASSERT_RETURN_VALUE(port > 0, -1);
	
	socket_t sock;

	if (!SocketOpenTCP(&sock))
	{
		std::cout << "(CReactor::CreateConnector)no resource";
		return -1;
	}

	if (!SocketSetNonBlocking(sock))
	{
		char info[128];
		std::cout << ("(CReactor::CreateConnector)set non blocking failed");
		std::cout << ( SocketGetError(info, sizeof(info)));
		 SocketClose(sock);
		return -1;
	}

	connector_t* pConnect = NEW connector_t;
	ASSERT_RETURN_VALUE(pConnect, -1);

	memset(pConnect, 0, sizeof(connector_t));
	pConnect->Socket = sock;
	pConnect->nInBufferLen = in_buf_len;
	pConnect->nOutBufferLen = out_buf_len;
	pConnect->nOutBufferMax = out_buf_max;
	pConnect->nSendEmpty = 1;
	pConnect->pInBuf = NEW char[in_buf_len];
	pConnect->pOutBuf = NEW char[out_buf_len];
	ASSERT_RETURN_VALUE(pConnect->pInBuf, -1);
	ASSERT_RETURN_VALUE(pConnect->pOutBuf, -1);
	pConnect->pSendBegin = pConnect->pOutBuf;
	pConnect->ConnectCallback = conn_cb;
	pConnect->CloseCallback = close_cb;
	pConnect->ReceiveCallback = recv_cb;
	pConnect->pContext = context;
	pConnect->nPort = port;
	strncpy(pConnect->strAddr, addr, sizeof(pConnect->strAddr));
	pConnect->nTimeout = timeout;

	size_t index;

	if (m_ConnectorFrees.empty())
	{
		index = m_Connectors.size();
		m_Connectors.push_back(pConnect);
	}
	else
	{
		index = m_ConnectorFrees.back();
		m_ConnectorFrees.pop_back();
		m_Connectors[index] = pConnect;
	}

	pConnect->nEvData = MAKE_EV_DATA(TYPE_CONNECTOR, index, sock);

	// 添加到EPOLL
	struct epoll_event ev;
	
	ev.data.u64 = pConnect->nEvData;
	ev.events = EPOLLOUT;

	if (epoll_ctl(m_Epoll, EPOLL_CTL_ADD, sock, &ev) != 0)
	{
		std::cout << ("(CReactor::CreateConnector)epoll add failed");
		DeleteConnector((int)index);
		return -1;
	}

	if (timeout > 0)
	{
		// 保存发送超时时间
		socklen_t len = sizeof(pConnect->tvTimeout);
		int res = getsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, 
			(char*)&pConnect->tvTimeout, &len);

		if (-1 == res)
		{
			char info[128];
			std::cout << ("(CReactor::CreateConnector)get send timeout error");
			std::cout << ( SocketGetError(info, sizeof(info)));
			DeleteConnector((int)index);
			return -1;
		}

		// 设置连接超时时间
		struct timeval tv = { timeout, 0 };
		
		res = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, 
			sizeof(tv));

		if (-1 == res)
		{
			char info[128];
			std::cout << ("(CReactor::CreateConnector)set connect timeout error");
			std::cout << ( SocketGetError(info, sizeof(info)));
			DeleteConnector((int)index);
			return -1;
		}
	}
	
	// 发起异步连接 
	if (!SocketConnect(sock, addr, port))
	{
		char info[128];
		std::cout << ("(CReactor::CreateConnector)connect failed");
		std::cout << ( SocketGetError(info, sizeof(info)));
		DeleteConnector((int)index);
		return -1;
	}
	
	return (int)index;
}

bool CReactor::DeleteConnector(int connector_id)
{
	ASSERT_RETURN_VALUE(size_t(connector_id) < m_Connectors.size(), false);

	connector_t* pConnect = m_Connectors[connector_id];

	if (NULL == pConnect)
	{
		return false;
	}

	if (pConnect->Socket != PORT_INVALID_SOCKET)
	{
		// 从EPOLL移除
		struct epoll_event ev;
		
		ev.data.fd = pConnect->Socket;
		ev.events = 0;
		
		if (epoll_ctl(m_Epoll, EPOLL_CTL_DEL, pConnect->Socket, &ev) != 0)
		{
			std::cout << ("(CReactor::DeleteConnector)epoll remove failed");
		}

		 SocketClose(pConnect->Socket);
	}

	delete[] pConnect->pOutBuf;
	delete[] pConnect->pInBuf;
	delete pConnect;
	m_Connectors[connector_id] = NULL;
	m_ConnectorFrees.push_back(connector_id);

	return true;
}

bool CReactor::ShutdownConnector(int connector_id)
{
	ASSERT_RETURN_VALUE(size_t(connector_id) < m_Connectors.size(), false);

	connector_t* pConnect = m_Connectors[connector_id];

	if (NULL == pConnect)
	{
		std::cout << ("(CReactor::ShutdownConnector)connect not exists");
		return false;
	}

	if (pConnect->Socket != PORT_INVALID_SOCKET)
	{
		if (!SocketShutdownSend(pConnect->Socket))
		{
			char info[128];
			std::cout << ("(CReactor::ShutdownConnector)shutdown failed");
			std::cout << ( SocketGetError(info, sizeof(info)));
			return false;
		}
	}

	return true;
}

bool CReactor::GetConnected(int connector_id)
{
	ASSERT_RETURN_VALUE(size_t(connector_id) < m_Connectors.size(), false);

	connector_t* pConnect = m_Connectors[connector_id];

	if (NULL == pConnect)
	{
		return false;
	}

	return (NULL == pConnect->ConnectCallback);
}

// 扩充发送缓冲区
static bool expand_out_buffer(CReactor::connector_t* pConnect, 
	size_t need_size, bool force)
{
	size_t send_remain = pConnect->nSendRemain;

	if (need_size < pConnect->nSendOffset)
	{
		if (send_remain > 0)
		{
			// 调试用，记录移动大块内存的日志
			if (send_remain > 0x100000)
			{
				char info[128];
				SafeSprintf(info, sizeof(info), 
					"(CReactor::expand_out_buffer)memmove big data %u bytes, %s:%d!",
					send_remain, pConnect->strAddr, pConnect->nPort);
				std::cout << (info);
			}
			
			// 将剩余需要发送的数据移动最前面
			memmove(pConnect->pOutBuf, pConnect->pSendBegin, send_remain);
		}

		pConnect->pSendBegin = pConnect->pOutBuf;
		pConnect->nSendOffset = 0;
	}
	else
	{
		size_t new_size = 0;

		port_memory_info_t mem_info;
		 GetMemoryInfo(&mem_info);
		if (mem_info.dMemoryLoad < 0.9) 
		{
			// 内存小于80%
			if (pConnect->nOutBufferLen >= 1024 * 1024 * 1024) 
			{
				// 大于1G时，修改分配策略
				new_size = pConnect->nOutBufferLen + 1024 * 1024 * 1024;
			}
			else
			{
				new_size = pConnect->nOutBufferLen * 2;
			}

			if (new_size < (need_size + send_remain))
			{
				new_size = need_size + send_remain;
			}
		}
		else
		{
			//_EchoInfo("The memory uses more than 90%! please start a backup server!!!");
			std::cout << ("The memory uses more than 90%! please start a backup server!!!");
		}

		if (new_size < (need_size + send_remain))
		{
			new_size = need_size + send_remain;
		}

		if ((pConnect->nOutBufferMax > 0) 
			&& (new_size > pConnect->nOutBufferMax))
		{
			if (!force)
			{
				return false;
			}
		}

		char* pNewBuf = NEW char[new_size];
		ASSERT_RETURN_VALUE(pNewBuf, false);

		if (pNewBuf == NULL)
		{
			std::cout << ("Can't New memory!");
			return false;
		}

		if (send_remain > 0)
		{
			memcpy(pNewBuf, pConnect->pSendBegin, send_remain);
		}

		delete[] pConnect->pOutBuf;
		pConnect->pOutBuf = pNewBuf;
		pConnect->nOutBufferLen = new_size;
		pConnect->pSendBegin = pNewBuf;
		pConnect->nSendOffset = 0;
	}

	return true;
}

/*
// 强制发送数据
bool CReactor::ForceSend(connector_t* pConnect, size_t need_size)
{
	size_t send_size = 0;
	socket_t sock = pConnect->Socket;
	
	while (pConnect->nSendRemain > 0)
	{
		// 发送数据
		int res = send(sock, pConnect->pSendBegin, pConnect->nSendRemain, 0);

		if (-1 == res)
		{
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)	
				|| (errno == EINTR))
			{
				// 等待可以再次发送
				struct pollfd pfd;

				pfd.fd = sock;
				pfd.events = POLLOUT;
				pfd.revents = 0;

				if (poll(&pfd, 1, -1) != 1)
				{
					std::cout << ("(CReactor::ForceSend)poll failed");
					return false;
				}

				continue;
			}
			else
			{
				char info[128];
				std::cout << ("(CReactor::ForceSend)send error");
				std::cout << ( SocketGetError(info, sizeof(info)));
				return false;
			}
		}
		else
		{
			char info[128];
			SafeSprintf(info, sizeof(info), 
				"(CReactor::ForceSend)send %d bytes", res);
			std::cout << (info);
		}

		ASSERT_RETURN_VALUE(size_t(res) <= pConnect->nSendRemain, false);

		pConnect->pSendBegin += res;
		pConnect->nSendOffset += res;
		pConnect->nSendRemain -= res;

		send_size += res;

		if (send_size >= need_size)
		{
			// 有足够的可用空间即返回，不作无限长时间的等待，防止程序死循环
			break;
		}
	}
	
	if (0 == pConnect->nSendRemain)
	{
		// 所有消息都已发送
		pConnect->nSendEmpty = 1;
		pConnect->pSendBegin = pConnect->pOutBuf;
		pConnect->nSendOffset = 0;

		// 现在只关注读取
		struct epoll_event ev;

		ev.data.u64 = pConnect->nEvData;
		ev.events = EPOLLIN | EPOLLRDHUP;

		if (epoll_ctl(m_Epoll, EPOLL_CTL_MOD, sock, &ev) != 0)
		{
			std::cout << ("(CReactor::ForceSend)epoll modify failed");
			return false;
		}
	}

	return true;
}

// 获得需要的发送缓冲空间
bool CReactor::GetSendSpace(connector_t* pConnect, size_t need_size, 
	bool force)
{
	// 尝试扩充发送缓冲区
	if (expand_out_buffer(pConnect, need_size))
	{
		return true;
	}

	if (!force)
	{
		return false;
	}

	// 强制发送掉当前缓冲区里的内容
	if (!ForceSend(pConnect, need_size))
	{
		return false;
	}

	//if (pConnect->nOutBufferLen >= need_size)
	//{
	//	return true;
	//}

	// 再次扩充发送缓冲区
	return expand_out_buffer(pConnect, need_size);
}
*/

bool CReactor::Send(int connector_id, const void* pdata, size_t len, 
	bool force)
{
	ASSERT_RETURN_VALUE(size_t(connector_id) < m_Connectors.size(), false);

	connector_t* pConnect = m_Connectors[connector_id];

	if (NULL == pConnect)
	{
		std::cout << ("(CReactor::Send)connect not exists");
		return false;
	}

	size_t used_size = pConnect->nSendOffset + pConnect->nSendRemain;

	if ((pConnect->nOutBufferLen - used_size) < len)
	{
		//if (!GetSendSpace(pConnect, len, force))
		if (!expand_out_buffer(pConnect, len, force))
		{
			char info[128];
			SafeSprintf(info, sizeof(info), 
				"(CReactor::Send)buffer overflow, need %d bytes", len);
			std::cout << (info);
			return false;
		}
		
		used_size = pConnect->nSendOffset + pConnect->nSendRemain;
	}

	memcpy(pConnect->pOutBuf + used_size, pdata, len);
	pConnect->nSendRemain += len;

	if (pConnect->nSendEmpty == 1)
	{
		// 等待可发送
		struct epoll_event ev;
		
		ev.data.u64 = pConnect->nEvData;
		ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
	
		if (epoll_ctl(m_Epoll, EPOLL_CTL_MOD, pConnect->Socket, &ev) != 0)
		{
			std::cout << ("(CReactor::Send)epoll modify failed");
			return false;
		}
			
		pConnect->nSendEmpty = 0;
	}

	return true;
}

bool CReactor::Send2(int connector_id, const void* pdata1, size_t len1, 
	const void* pdata2, size_t len2, bool force)
{
	ASSERT_RETURN_VALUE(size_t(connector_id) < m_Connectors.size(), false);

	connector_t* pConnect = m_Connectors[connector_id];

	if (NULL == pConnect)
	{
		std::cout << ("(CReactor::Send2)connect not exists");
		return false;
	}

	size_t len = len1 + len2;
	size_t used_size = pConnect->nSendOffset + pConnect->nSendRemain;

	if ((pConnect->nOutBufferLen - used_size) < len)
	{
		//if (!GetSendSpace(pConnect, len, force))
		if (!expand_out_buffer(pConnect, len, force))
		{
			char info[128];
			SafeSprintf(info, sizeof(info), 
				"(CReactor::Send2)buffer overflow, need %d bytes", len);
			std::cout << (info);
			return false;
		}

		used_size = pConnect->nSendOffset + pConnect->nSendRemain;
	}

	char* p = pConnect->pOutBuf + used_size;
	memcpy(p, pdata1, len1);
	p += len1;
	memcpy(p, pdata2, len2);
	pConnect->nSendRemain += len;

	if (pConnect->nSendEmpty == 1)
	{
		// 等待可发送
		struct epoll_event ev;
		
		ev.data.u64 = pConnect->nEvData;
		ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
	
		if (epoll_ctl(m_Epoll, EPOLL_CTL_MOD, pConnect->Socket, &ev) != 0)
		{
			std::cout << ("(CReactor::Send2)epoll modify failed");
			return false;
		}

		pConnect->nSendEmpty = 0;
	}

	return true;
}

bool CReactor::Send3(int connector_id, const void* pdata1, size_t len1, 
	const void* pdata2, size_t len2, const void* pdata3, size_t len3, 
	bool force)
{
	ASSERT_RETURN_VALUE(size_t(connector_id) < m_Connectors.size(), false);

	connector_t* pConnect = m_Connectors[connector_id];

	if (NULL == pConnect)
	{
		std::cout << ("(CReactor::Send3)connect not exists");
		return false;
	}

	size_t len = len1 + len2 + len3;
	size_t used_size = pConnect->nSendOffset + pConnect->nSendRemain;

	if ((pConnect->nOutBufferLen - used_size) < len)
	{
		//if (!GetSendSpace(pConnect, len, force))
		if (!expand_out_buffer(pConnect, len, force))
		{
			char info[128];
			SafeSprintf(info, sizeof(info), 
				"(CReactor::Send3)buffer overflow, need %d bytes", len);
			std::cout << (info);
			return false;
		}

		used_size = pConnect->nSendOffset + pConnect->nSendRemain;
	}

	char* p = pConnect->pOutBuf + used_size;
	memcpy(p, pdata1, len1);
	p += len1;
	memcpy(p, pdata2, len2);
	p += len2;
	memcpy(p, pdata3, len3);
	pConnect->nSendRemain += len;

	if (pConnect->nSendEmpty == 1)
	{
		// 等待可发送
		struct epoll_event ev;
		
		ev.data.u64 = pConnect->nEvData;
		ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
	
		if (epoll_ctl(m_Epoll, EPOLL_CTL_MOD, pConnect->Socket, &ev) != 0)
		{
			std::cout << ("(CReactor::Send3)epoll modify failed");
			return false;
		}

		pConnect->nSendEmpty = 0;
	}

	return true;
}

int CReactor::CreateTimer(float seconds, timer_callback cb, void* context)
{
	ASSERT_RETURN_VALUE(seconds > 0.0f, -1);

	if (seconds < 0.001f)
	{
		return -1;
	}

	timer_t* pTimer = NEW timer_t;
	ASSERT_RETURN_VALUE(pTimer, -1);

	pTimer->fSeconds = seconds;
	pTimer->fCounter = 0.0f;
	pTimer->TimerCallback = cb;
	pTimer->pContext = context;
	
	size_t index;

	if (m_TimerFrees.empty())
	{
		index = m_Timers.size();
		m_Timers.push_back(pTimer);
	}
	else
	{
		index = m_TimerFrees.back();
		m_TimerFrees.pop_back();
		m_Timers[index] = pTimer;
	}

	UpdateMinTime();

	return (int)index;
}

bool CReactor::DeleteTimer(int timer_id)
{
	ASSERT_RETURN_VALUE(size_t(timer_id) < m_Timers.size(), false);

	timer_t* pTimer = m_Timers[timer_id];

	if(pTimer != NULL)
	{
		delete pTimer;
		m_Timers[timer_id] = NULL;
		m_TimerFrees.push_back(timer_id);

		UpdateMinTime();
		return true;
	}
	return false;
}

void CReactor::UpdateMinTime()
{
	float min_time = 1.0f;
	size_t timer_size = m_Timers.size();

	for (size_t i = 0; i < timer_size; ++i)
	{
		timer_t* pTimer = m_Timers[i];

		if (NULL == pTimer)
		{
			continue;
		}

		if (pTimer->fSeconds < min_time)
		{
			min_time = pTimer->fSeconds;
		}
	}

	if (min_time < 1.0f)
	{
		m_fMinTime = min_time;
	}
	else
	{
		m_fMinTime = 1.0f;
	}
}

bool CReactor::CloseConnect(size_t index)
{
	ASSERT_RETURN_VALUE(index < m_Connectors.size(), false);
	
	connector_t* pConnect = m_Connectors[index];

	if (NULL == pConnect)
	{
		return false;
	}

	close_callback cb = pConnect->CloseCallback;
	void* context = pConnect->pContext;
	int port = pConnect->nPort;
	char addr[32];
	strncpy(addr, sizeof(addr), pConnect->strAddr);

	DeleteConnector((int)index);

	// 连接关闭通知
	cb(context, (int)index, addr, port);
	
	return true;
}

void CReactor::EventLoop()
{
	unsigned int nEventTick =  GetTickCount();
	unsigned int nTotalTick = nEventTick;
	unsigned int nReadTick  = 0;
	unsigned int nWirteTick = 0;
	unsigned int nErrorTick = 0;

	int nfds = epoll_wait(m_Epoll, m_pEvents, m_nEventsPerLoop, 1);

	nEventTick =  GetTickCount() - nEventTick;
	
	if (nfds > 0)
	{
		for (int n = 0; n < nfds; ++n)
		{
			struct epoll_event* ev = &m_pEvents[n];
			int events = ev->events;
			UINT64 ev_data = ev->data.u64;
			unsigned int index = ev_data >> 32;
			unsigned int sock = ev_data & 0xFFFFFFFF;
			unsigned int nTempTick =  GetTickCount();

			if (events & EPOLLIN)
			{
				ProcessRead(index, sock);

				nTempTick =  GetTickCount() - nTempTick;
				nReadTick += nTempTick;
			}
			
			nTempTick =  GetTickCount();
			if (events & EPOLLOUT)
			{
				ProcessWrite(index, sock);

				nTempTick =  GetTickCount() - nTempTick;
				nWirteTick += nTempTick;
			}
			
			nTempTick =  GetTickCount();
			if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
			{
				ProcessError(index, sock, events);

				nTempTick =  GetTickCount() - nTempTick;
				nErrorTick += nTempTick;
			}
		}
		
		if (nfds == m_nEventsPerLoop)
		{
			if (m_nEventsPerLoop < MAX_EVENTS_PER_LOOP)
			{
				// 通讯繁忙时增加每次检测的事件数量
				size_t new_event_num = m_nEventsPerLoop * 2;
				struct epoll_event* new_events = 
					NEW struct epoll_event[new_event_num];
				ASSERT_RETURN(new_events);

				if (new_events)
				{
					delete[] m_pEvents;
					m_pEvents = new_events;
					m_nEventsPerLoop = new_event_num;
				}
			}
		}
	}
	else if (nfds < 0)
	{
		if (errno != EINTR)
		{
			std::cout << ("(CReactor::EventLoop)epoll_wait error. errno:%d ", errno);
			if (nEventTick <= 0)
			{	//休眠1毫秒，避免CPU100%
				usleep(1000);
			}
		}
	}

	if (m_nEventsPerLoop >= MAX_EVENTS_PER_LOOP)
	{
		printf("m_nEventsPerLoop>=MAX_EVENTS_PER_LOOP(%d)", MAX_EVENTS_PER_LOOP);
		ASSERT_RETURN(0);
	}

	unsigned int nTimerTick =  GetTickCount();

	float elapse = (float)m_pTimer->GetElapseMillisec() * 0.001f;
	
	// 检查所有的定时器
	if (!m_Timers.empty())
	{
		size_t timer_size = m_Timers.size();

		for (size_t i = 0; i < timer_size; ++i)
		{
			timer_t* pTimer = m_Timers[i];

			if (NULL == pTimer)
			{
				continue;
			}

			pTimer->fCounter += elapse;

			if (pTimer->fCounter >= pTimer->fSeconds)
			{
				float seconds = pTimer->fCounter;

				pTimer->fCounter = 0.0f;
				pTimer->TimerCallback(pTimer->pContext, (int)i, seconds);
			}
		}
	}

	nTimerTick =  GetTickCount() - nTimerTick;

	nTotalTick =  GetTickCount() - nTotalTick;

	if (nTotalTick > 500)
	{
		std::cout << ("EvnetLoop running exceed %ums, event %ums read %ums wirte %ums err %ums timer %ums\n",
			nTotalTick, nEventTick, nReadTick, nWirteTick, nErrorTick, nTimerTick);
	}

}

bool CReactor::ProcessWrite(size_t index, int sock)
{
	ASSERT_RETURN_VALUE(((index >> 24) & 0xFF) == 0, false);

	connector_t* pConnect = m_Connectors[index];

	if (NULL == pConnect)
	{
		return false;
	}
	
	if (pConnect->Socket != sock)
	{
		std::cout << ("(CReactor::ProcessWrite)sock id not match");
		return false;
	}

	if (pConnect->ConnectCallback)
	{
		if (pConnect->nTimeout > 0)
		{
			// 恢复发送超时时间
			int res = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, 
				(char*)&pConnect->tvTimeout, sizeof(pConnect->tvTimeout));

			if (-1 == res)
			{
				char info[128];
				std::cout << ("(CReactor::ProcessWrite)set send timeout error");
				std::cout << ( SocketGetError(info, sizeof(info)));
			}
		}
		
		connect_callback cb = pConnect->ConnectCallback;
		
		// 清除这个回调指针表示连接已经完成
		pConnect->ConnectCallback = NULL;

		int error = 0;
		socklen_t len = sizeof(error);
		int res = getsockopt(sock, SOL_SOCKET, SO_ERROR,
			&error, &len);

		if ((res < 0) || (error != 0))
		{
			// 连接失败通知
			cb(pConnect->pContext, (int)index, 0, pConnect->strAddr, 
				pConnect->nPort);

			std::cerr << ("(CReactor::ProcessWrite), err:%d",errno);

			// 立即删除之
			DeleteConnector((int)index);

			return false;
		}
		else
		{
			// 连接成功通知
			cb(pConnect->pContext, (int)index, 1, pConnect->strAddr, 
				pConnect->nPort);
		}
		
	}
	
	if (pConnect->nSendRemain > 0)
	{
		// 发送数据
		size_t send_len = pConnect->nSendRemain;

		if (send_len > 0x10000)
		{
			// 不要一次发送太多的数据
			send_len = 0x10000;
		}

		int res = send(sock, pConnect->pSendBegin, send_len, MSG_NOSIGNAL);
	
		if (-1 == res)
		{
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)	
				|| (errno == EINTR))
			{
				res = 0;
			}
			else
			{
				char info[128];
				std::cout << ("(CReactor::ProcessWrite)send error");
				std::cout << ( SocketGetError(info, sizeof(info)));
				CloseConnect(index);
				return false;
			}
		}
		
		ASSERT_RETURN_VALUE(size_t(res) <= send_len, false);
		
		pConnect->pSendBegin += res;
		pConnect->nSendOffset += res;
		pConnect->nSendRemain -= res;
	}
	
	if (0 == pConnect->nSendRemain)
	{
		if ((pConnect->nOutBufferMax > 0)
			&& (pConnect->nOutBufferLen > pConnect->nOutBufferMax))
		{
			// 收缩缓冲区
			char* pNewBuf = NEW char[pConnect->nOutBufferMax];
			ASSERT_RETURN_VALUE(pNewBuf, false);
			SAFE_DELETE_ARRAY(pConnect->pOutBuf);
			pConnect->nOutBufferLen = pConnect->nOutBufferMax;
			pConnect->pOutBuf = pNewBuf;
		}

		// 所有消息都已发送
		pConnect->nSendEmpty = 1;
		pConnect->pSendBegin = pConnect->pOutBuf;
		pConnect->nSendOffset = 0;
		
		// 现在只关注读取
		struct epoll_event ev;

		ev.data.u64 = pConnect->nEvData;
		ev.events = EPOLLIN | EPOLLRDHUP;

		if (epoll_ctl(m_Epoll, EPOLL_CTL_MOD, sock, &ev) != 0)
		{
			std::cout << ("(CReactor::ProcessWrite)epoll modify failed");
			return false;
		}
	}

	return true;
}

bool CReactor::ProcessRead(size_t index, int sock)
{
	int io_type = (index >> 24) & 0xFF;

	if (io_type == TYPE_CONNECTOR)
	{
		connector_t* pConnect = m_Connectors[index];
		
		if (NULL == pConnect)
		{
			return false;
		}
		
		if (pConnect->Socket != sock)
		{
			std::cout << ("(CReactor::ProcessRead)sock id not match");
			return false;
		}

		int res = recv(sock, pConnect->pInBuf, pConnect->nInBufferLen, 0);

		while(res > 0)
		{
			// 处理接收到的数据
			pConnect->ReceiveCallback(pConnect->pContext, (int)index,
				pConnect->pInBuf, res);

			if ( m_Connectors[index] == NULL )
			{
				return false;
			}

			res = recv(sock, pConnect->pInBuf, pConnect->nInBufferLen, 0);
		}

		if (-1 == res && errno != EAGAIN)
		{
			char info[128];
			std::cout << ("(CReactor::ProcessRead)recv error");
			std::cout << ( SocketGetError(info, sizeof(info)));
			CloseConnect(index);
			return false;
		}

		if (0 == res)
		{
			// 连接已经关闭
			CloseConnect(index);
			return false;
		}

		return true;
	}

	if (io_type == TYPE_LISTENER)
	{
		index &= 0x00FFFFFF;
		listener_t* pListener = m_Listeners[index];

		ASSERT_RETURN_VALUE(pListener != NULL, false);

		if (NULL == pListener)
		{
			return false;
		}

		if (pListener->Socket != sock)
		{
			std::cout << ("(CReactor::ProcessRead)sock id not match");
			return false;
		}

		sockaddr_in sa;
		socklen_t len = sizeof(sockaddr);

		socket_t new_sock = accept(sock, (sockaddr*)&sa, &len); 

		if (PORT_INVALID_SOCKET == new_sock)
		{
			char info[128];
			std::cout << ("(CReactor::ProcessRead)accept failed");
			std::cout << ( SocketGetError(info, sizeof(info)));
			return false;
		}

		// 设置为非阻塞模式
		if (!SocketSetNonBlocking(new_sock))
		{
			char info[128];
			std::cout << ("(CReactor::ProcessAccept)set nonblocking failed");
			std::cout << ( SocketGetError(info, sizeof(info)));
			 SocketClose(new_sock);
			return false;
		}

		connector_t* pConnect = NEW connector_t;
		ASSERT_RETURN_VALUE(pConnect, false);

		memset(pConnect, 0, sizeof(connector_t));
		pConnect->Socket = new_sock;
		pConnect->nInBufferLen = pListener->nInBufferLen;
		pConnect->nOutBufferLen = pListener->nOutBufferLen;
		pConnect->nOutBufferMax = pListener->nOutBufferMax;
		pConnect->nSendEmpty = 1;
		pConnect->pInBuf = NEW char[pListener->nInBufferLen];
		pConnect->pOutBuf = NEW char[pListener->nOutBufferLen];
		ASSERT_RETURN_VALUE(pConnect->pInBuf, false);
		ASSERT_RETURN_VALUE(pConnect->pOutBuf, false);
		pConnect->pSendBegin = pConnect->pOutBuf;
		strncpy(pConnect->strAddr, inet_ntoa(sa.sin_addr), sizeof(pConnect->strAddr));
		pConnect->nPort = ntohs(sa.sin_port);
		pConnect->CloseCallback = pListener->CloseCallback;
		pConnect->ReceiveCallback = pListener->ReceiveCallback;
		pConnect->pContext = pListener->pContext;

		size_t connect_index;

		if (m_ConnectorFrees.empty())
		{
			connect_index = m_Connectors.size();
			m_Connectors.push_back(pConnect);
		}
		else
		{
			connect_index = m_ConnectorFrees.back();
			m_ConnectorFrees.pop_back();
			m_Connectors[connect_index] = pConnect;
		}

		pConnect->nEvData = MAKE_EV_DATA(TYPE_CONNECTOR, connect_index, 
			new_sock);

		// 添加到EPOLL
		struct epoll_event ev;

		ev.data.u64 = pConnect->nEvData;
		ev.events = EPOLLIN | EPOLLRDHUP;

		if (epoll_ctl(m_Epoll, EPOLL_CTL_ADD, new_sock, &ev) != 0)
		{
			std::cout << ("(CReactor::ProcessRead)epoll add failed");
			DeleteConnector((int)connect_index);
			return false;
		}

		// 接受连接通知
		pListener->AcceptCallback(pListener->pContext, (int)index,
			(int)connect_index, pConnect->strAddr, pConnect->nPort);

		return true;
	}

	if (io_type == TYPE_BROADCAST)
	{
		index &= 0x00FFFFFF;
		broadcast_t* pBroad = m_Broadcasts[index];

		if (NULL == pBroad)
		{
			return false;
		}

		if (pBroad->Socket != sock)
		{
			std::cout << ("(CReactor::ProcessRead)sock id not match");
			return false;
		}

		size_t read_size;
		char remote_addr[32];
		int remote_port;

		if (!SocketReceiveFrom(sock, pBroad->pInBuf, pBroad->nInBufferLen,
			remote_addr, sizeof(remote_addr), &remote_port, &read_size))
		{
			char info[128];
			std::cout << ("(CReactor::ProcessBroad)receive from error");
			std::cout << ( SocketGetError(info, sizeof(info)));
			return false;
		}

		pBroad->BroadcastCallback(pBroad->pContext, (int)index,
			remote_addr, remote_port, pBroad->pInBuf, read_size);

		return true;
	}

	return false;
}

bool CReactor::ProcessError(size_t index, int sock, int events)
{
	int io_type = (index >> 24) & 0xFF;

	if (io_type == TYPE_CONNECTOR)
	{
		if (0 == (events & (EPOLLERR | EPOLLHUP)))
		{
			return true;
		}

		connector_t* pConnect = m_Connectors[index];

		if (NULL == pConnect)
		{
			return false;
		}
		
		if (pConnect->Socket != sock)
		{
			std::cout << ("(CReactor::ProcessError)sock id not match");
			return false;
		}
		
		if (pConnect->ConnectCallback)
		{
			// 连接失败通知
			pConnect->ConnectCallback(pConnect->pContext, (int)index, 0, 
				pConnect->strAddr, pConnect->nPort);

			std::cerr << ("(CReactor::ProcessError), err:%d",errno);

			// 立即删除之
			DeleteConnector((int)index);
		}
		else
		{
			if (events & EPOLLERR)
			{
				std::cout << ("(CReactor::ProcessError)socket error");
			}
			
			if (events & EPOLLHUP)
			{
				std::cout << ("(CReactor::ProcessError)socket hangup");
			}
			
			if (events & EPOLLRDHUP)
			{
				std::cout << ("(CReactor::ProcessError)socket read hangup");
			}
			
			CloseConnect(index);
		}
		
		return true;
	}

	if (io_type == TYPE_LISTENER)
	{
		std::cout << ("(CReactor::ProcessError)listener error");
		return true;
	}
	
	if (io_type == TYPE_BROADCAST)
	{
		std::cout << ("(CReactor::ProcessError)broadcast error");
		return true;
	}

	return false;
}

// 设置指定连接的context
bool CReactor::SetContext(int connector_id, void* context)
{
	ASSERT_RETURN_VALUE(size_t(connector_id) < m_Connectors.size(), false);

	connector_t* pConnect = m_Connectors[connector_id];

	if (NULL == pConnect)
	{
		std::cout << ("(CReactor::SetContext)connect not exists");
		return false;
	}

	pConnect->pContext = context;

	return true;
}

bool CReactor::Dump(const char* file_name)
{
	FILE* fp =  FileOpen(file_name, "wb");

	if (NULL == fp)
	{
		return false;
	}

	fprintf(fp, "events per loop is %d\r\n", m_nEventsPerLoop);
	fprintf(fp, "minimum timer is %f\r\n", (double)m_fMinTime);
	fprintf(fp, "\r\n");

	for (size_t k1 = 0; k1 < m_Broadcasts.size(); ++k1)
	{
		broadcast_t* p = m_Broadcasts[k1];

		if (NULL == p)
		{
			continue;
		}

		fprintf(fp, "broadcast local addr:%s, broad addr:%s, port:%d, "
			"in buffer len:%d\r\n", p->strLocalAddr, p->strBroadAddr,
			p->nPort, p->nInBufferLen);
	}

	fprintf(fp, "\r\n");

	for (size_t k2 = 0; k2 < m_Listeners.size(); ++k2)
	{
		listener_t* p = m_Listeners[k2];

		if (NULL == p)
		{
			continue;
		}

		fprintf(fp, "listener addr:%s, port:%d, in buffer len:%d, "
			"out buffer len:%d, out buffer max:%d\r\n", p->strAddr, p->nPort,
			p->nInBufferLen, p->nOutBufferLen, p->nOutBufferMax);
	}

	fprintf(fp, "\r\n");

	for (size_t k3 = 0; k3 < m_Connectors.size(); ++k3)
	{
		connector_t* p = m_Connectors[k3];

		if (NULL == p)
		{
			continue;
		}

		fprintf(fp, "connector addr:%s, port:%d, in buffer len:%d, "
			"out buffer len:%d, out buffer max:%d, send empty:%d, "
			"send remain:%d, send offset:%d\r\n", 
			p->strAddr, p->nPort, p->nInBufferLen, p->nOutBufferLen, 
			p->nOutBufferMax, p->nSendEmpty, p->nSendRemain, p->nSendOffset);
	}

	fprintf(fp, "\r\n");

	for (size_t k4 = 0; k4 < m_Timers.size(); ++k4)
	{
		timer_t* p = m_Timers[k4];

		if (NULL == p)
		{
			continue;
		}

		fprintf(fp, "timer seconds:%f, counter:%f\r\n", (double)p->fSeconds, 
			(double)p->fCounter);
	}

	fprintf(fp, "\r\n");

	fclose(fp);

	return true;
}


