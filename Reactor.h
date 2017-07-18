//--------------------------------------------------------------------

#ifndef _TOOLS_LINUXREACTOR_H
#define _TOOLS_LINUXREACTOR_H

#include <cstddef>
#include <vector>
#include <list>
#include <map>
#include "TypeDef.h"

// 内部通讯服务

// 接收到广播的回调
typedef void (*broadcast_callback)(void* context, int broadcast_id,
	const char* addr, int port, const void* pdata, size_t len);
// 接受连接的回调
typedef void (*accept_callback)(void* context, int listener_id, 
	int connector_id, const char* addr, int port);
// 主动连接完成的回调
typedef void (*connect_callback)(void* context, int connector_id,
	int succeed, const char* addr, int port);
// 连接关闭的回调
typedef void (*close_callback)(void* context, int connector_id, 
	const char* addr, int port);
// 接收到消息的回调
typedef void (*receive_callback)(void* context, int connector_id, 
	const void* pdata, size_t len);
// 定时器回调
typedef void (*timer_callback)(void* context, int timer_id, float seconds);

unsigned int GetTickCount();

class CTickTimer;
struct epoll_event;

class CReactor
{
public:
	struct broadcast_t;
	struct listener_t;
	struct connector_t;
	struct timer_t;
	struct udp_t;

public:
	CReactor();
	~CReactor();

	// 启动
	bool Start();
	// 停止
	bool Stop();

	// 创建广播
	int CreateBroadcast(const char* local_addr, const char* broad_addr,
		int port, size_t in_buf_len, broadcast_callback cb, void* context);
	// 关闭广播
	bool DeleteBroadcast(int broadcast_id);
	// 发送广播消息
	bool SendBroadcast(int broadcast_id, const void* pdata, size_t len);

	// 创建侦听
	int CreateListener(const char* addr, int port, int backlog, 
		size_t in_buf_len, size_t out_buf_len, size_t out_buf_max, 
		accept_callback accept_cb, close_callback close_cb, 
		receive_callback recv_cb, void* context, size_t accept_num);
	// 结束侦听
	bool DeleteListener(int listener_id);
	// 获得侦听句柄
	size_t GetListenerSock(int listener_id);
	// 获得实际侦听的端口
	int GetListenerPort(int listener_id);

	// udp 点对点
	bool InitUdp(const char* addr, int port);
	bool SendUdp(const void* pdata, size_t len);
	bool DeleteUdp();

	// 创建主动连接
	int CreateConnector(const char* addr, int port, size_t in_buf_len, 
		size_t out_buf_len, size_t out_buf_max, int timeout, 
		connect_callback conn_cb, close_callback close_cb, 
		receive_callback recv_cb, void* context);
	// 删除连接
	bool DeleteConnector(int connector_id);
	// 关闭连接
	bool ShutdownConnector(int connector_id);
	// 连接是否完成
	bool GetConnected(int connector_id);
	// 发送消息
	bool Send(int connector_id, const void* pdata, size_t len, bool force);
	bool Send2(int connector_id, const void* pdata1, size_t len1, 
		const void* pdata2, size_t len2, bool force);
	bool Send3(int connector_id, const void* pdata1, size_t len1, 
		const void* pdata2, size_t len2, const void* pdata3, size_t len3, 
		bool force);
	// 设置指定连接的context
	bool SetContext(int connector_id, void* context);

	// 创建定时器
	int CreateTimer(float seconds, timer_callback cb, void* context);
	// 删除定时器
	bool DeleteTimer(int timer_id);

	// 事件循环
	void EventLoop();

	// 导出信息
	bool Dump(const char* file_name);

private:
	// 更新最小定时时间
	void UpdateMinTime();
	// 关闭连接
	bool CloseConnect(size_t index);

	// 强制发送数据
	//bool ForceSend(connector_t* pConnect, size_t need_size);
	// 获得需要的发送缓冲空间
	//bool GetSendSpace(connector_t* pConnect, size_t need_size, bool force);
	
	// 处理读取信号
	bool ProcessRead(size_t index, int sock);
	// 处理写入信号
	bool ProcessWrite(size_t index, int sock);
	// 处理错误信号
	bool ProcessError(size_t index, int sock, int events);

private:
	int m_nEventsPerLoop;
	struct epoll_event* m_pEvents;
	int m_Epoll;
	float m_fMinTime;
	//CTickTimer* m_pTimer;
	udp_t* m_pUdp;
	std::vector<broadcast_t*> m_Broadcasts;
	std::vector<size_t> m_BroadcastFrees;
	std::vector<listener_t*> m_Listeners;
	std::vector<size_t> m_ListenerFrees;
	std::vector<connector_t*> m_Connectors;
	std::vector<size_t> m_ConnectorFrees;
	std::vector<timer_t*> m_Timers;
	std::vector<size_t> m_TimerFrees;
};

#endif // _TOOLS_LINUXREACTOR_H

