// filename: threadpool.h
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"

// �̳߳��࣬���䶨��Ϊģ������Ϊ�˴��븴�ã�ģ�����T��������
template <class T>
class threadpool
{
public:
	// thread_number���̳߳����̵߳�����
	// max_requests������������������ġ��ȴ���������������
	threadpool(int thread_number = 8, int max_requests = 10000);
	~threadpool();

	// ������������������
	bool append(T* request);

private:
	// �����߳����еĺ����������ϴӹ���������ȡ������ִ��֮
	static void* worker(void* arg);
	void run();

private:
	unsigned int m_thread_number; // �̳߳��е��߳���
	unsigned int m_max_requests;  // �����������������������
	pthread_t* m_threads; // �����̳߳ص����飬��СΪm_thread_number
	std::list<T*> m_workqueue; // �������
	locker m_queuelocker;// ����������еĻ�����
	sem m_queuestat;     // �ź������Ƿ���������Ҫ����
	bool m_stop;		 // �Ƿ�����߳�
};

template <class T>
threadpool<T>::threadpool(int thread_number, int max_requests) :
	m_thread_number(thread_number), m_max_requests(max_requests),
	m_stop(false), m_threads(nullptr)
{
	if ((thread_number <= 0) || (max_requests <= 0))
		throw std::exception();

	m_threads = new pthread_t[m_thread_number];
	if (!m_threads)
		throw std::exception();

	// ����thread_number���̣߳��������Ƕ�����Ϊ�����߳�
	for (int i(0); i < thread_number; ++i)
	{
		printf("create the %dth thread\n", i);
		if (pthread_create(m_threads + i, NULL, worker, this) != 0)
		{	// ���ʧ�ܣ���rollback
			delete[] m_threads;
			throw std::exception();
		}

		if (pthread_detach(m_threads[i]))
		{
			delete[] m_threads;
			throw std::exception();
		}
	}
}

template <class T>
threadpool<T>::~threadpool()
{
	delete[] m_threads;
	m_stop = true;
}

template <class T>
bool threadpool<T>::append(T* request)
{
	// �����������m_workqueueʱһ��Ҫ��������Ϊ���������̹߳���
	m_queuelocker.lock();
	if (m_workqueue.size() > m_max_requests)
	{
		m_queuelocker.unlock();
		return false;
	}
	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post(); // �ź���P����
	return true;
}

template <class T>
void* threadpool<T>::worker(void* arg)
{
	threadpool* pool = static_cast<threadpool*>(arg);
	pool->run();
	return pool;
}

template <class T>
void threadpool<T>::run()
{
	while (!m_stop)
	{
		m_queuestat.wait();
		m_queuelocker.lock();

		if (m_workqueue.empty())
		{
			m_queuelocker.unlock();
			continue;
		}

		// ȡ������
		T* request = m_workqueue.front();
		m_workqueue.pop_front();
		m_queuelocker.unlock();

		if (!request)
			continue;

		request->process();
	}
}

#endif // !THREADPOOL_H