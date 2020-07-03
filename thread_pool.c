
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

//node head insert
//使用宏，nWorkers与nJobs可通用一个LL_ADD,同时操作一个性质
#define LL_ADD(item,  list) do{   		  \
	item->next = list;        			   \
	item->prev = NULL;        			    \
	list = item;                              \
}while(0)


//node head remove
//使用宏，nWorkers与nJobs可通用一个LL_REMOVE
/*if(item == list) list = item->next;    如果是头节点，则后移动*/
	
//如果编译warring: backslash and newline separated by space	,\后面多了一个空格

#define LL_REMOVE(item, list) do{                          \
	if(item->prev != NULL) item->prev->next = item->next;  \
	if(item->next != NULL) item->next->prev = item->prev;   \
	if(item == list) list = item->next;                      \
	item->prev = item->next = NULL;                          \
}while(0)

//thread struct
typedef struct NWORKER{
	pthread_t thread_id;     //线程id
	int terminate;   //退出标志
	int busy;       //1--表示线程正在工作
	struct NMANGER *pool;  //可以拿到管理组件
	
	struct NWORKER *next;
	struct NWORKER *prev;	
}nworker;

//tast struct ;
typedef struct NJOB{
	//任务回调
	void (*func)(void *arg);
	void *user_data;  //用户数据信息
	
	struct NJOB *next;
	struct NJOB *prev;
}nJob;

//线程池管理组件,添加动态增加和删除线程功能
typedef struct NMANGER{
	nworker *workers;
	nJob *jobs;
	
	int sum_threads;   //线程总数
	int free_threads; //空闲线程数
	int terminate; 
	pthread_t monitorTid;
	//void (*monitorCallback)(void *arg);   //用于监控sum_threads和free_threads
	//互斥锁
	pthread_mutex_t jobs_mtx;
	pthread_cond_t jobs_cond;
}nManager;	

typedef nManager nThreadPool;
void *nWorkerCallback(void *arg);

//线程监控函数----动态检测
void *monitorThread(void *arg)
{
	int free_threads = 0;
	int sum_threads = 0;
	nThreadPool *pool = (nThreadPool *)arg;
	while(1){
		sleep(5);
		if(pool->terminate == 1){
			break;
		}
		pthread_mutex_lock(&pool->jobs_mtx);
		free_threads = pool->free_threads;
		sum_threads = pool->sum_threads;
		pthread_mutex_unlock(&pool->jobs_mtx);

		printf("sumThreadNum:%d, freeThreadNum:%d\n", pool->sum_threads, pool->free_threads);
	   if(free_threads <= (sum_threads * 0.4)){
	   		printf("free_thread: %d\n",free_threads);
			int i = 0;
			//增加一倍
			for(i = 0; i < sum_threads;i++){
				nworker *worker = malloc(sizeof(nworker));
				if(worker == NULL){
					perror("malloc");
					break;
				}
				memset(worker, 0, sizeof(nworker));
				int ret = pthread_create(&worker->thread_id, NULL, nWorkerCallback, worker);
				if(ret){
					perror("pthread_create");
					break;
				}
				pthread_mutex_lock(&pool->jobs_mtx);
				worker->pool = pool;
				pool->sum_threads ++;
				pool->free_threads ++;
		
				LL_ADD(worker, pool->workers);
				pthread_mutex_unlock(&pool->jobs_mtx);
			}
		}

		//减少1/2,如果总数小于10，则不需要减少,使用此方法释放，存在一个问题，需要等到worker队列线程执行完成后才能退出，
		//浪费时间，后续用一等待队列实现---优化
		else if(free_threads >= (pool->sum_threads * 0.8) && sum_threads > 10){
			nworker *w = pool->workers;
			int i;
			for(i = 0; i < sum_threads / 2;i++){
				pthread_mutex_lock(&pool->jobs_mtx);
				if(w->busy== 0){
					w->terminate = 1;
					pool->sum_threads--;
					pool->free_threads--;
					//w = w->next;
					pthread_cond_signal(&pool->jobs_cond);
					pthread_mutex_unlock(&pool->jobs_mtx);
				}
				w = w->next;
			}

		}
	}
}

void *nWorkerCallback(void *arg)
{
	nworker *worker = (nworker *)arg;

	while(1){
		pthread_mutex_lock(&worker->pool->jobs_mtx);
		while(worker->pool->jobs == NULL)	{
			//条件等待，休眠  unlock
			if(worker->terminate == 1) break;
			pthread_cond_wait(&worker->pool->jobs_cond, &worker->pool->jobs_mtx);
		}

		if(worker->terminate == 1) {
			pthread_mutex_unlock(&worker->pool->jobs_mtx);
			break;
		}
		nJob *job = worker->pool->jobs;
		if(job != NULL){
			LL_REMOVE(job, worker->pool->jobs);
		}
		pthread_mutex_unlock(&worker->pool->jobs_mtx);

		if(job == NULL) continue;
		worker->busy = 1;
		
		worker->pool->free_threads--;
		job->func(job);
		worker->pool->free_threads++;
		worker->busy = 0;
	}
	//printf("pthread_id %ld exit\n", worker->thread_id);
	free(worker);
	worker = NULL;
    pthread_exit(NULL);
}

//线程池初始化
int thread_pool_create(nThreadPool *pool, int numWorkers)
{
	if(pool == NULL) return 0;
	if(numWorkers < 1) numWorkers = 1;
	memset(pool, 0, sizeof(nThreadPool));

	//初始化mutex
	pthread_mutex_t blank_mutex = PTHREAD_MUTEX_INITIALIZER; 
	memcpy(&pool->jobs_mtx, &blank_mutex, sizeof(pthread_mutex_t));

	//初始化cond
	pthread_cond_t blank_cond = PTHREAD_COND_INITIALIZER;
	memcpy(&pool->jobs_cond, &blank_cond, sizeof(pthread_cond_t));

	//创建工作线程
	int i= 0;
	for(i = 0;i < numWorkers;i++){
		nworker *worker = malloc(sizeof(nworker));
		if(worker == NULL){
			perror("malloc");
			goto _exit;
		}
		memset(worker, 0, sizeof(nworker));
		int ret = pthread_create(&worker->thread_id, NULL, nWorkerCallback, worker);
		if(ret){
			perror("pthread_create");
			goto _exit;
		}
		worker->pool = pool;
		pool->sum_threads = numWorkers;
		pool->free_threads = numWorkers;
		
		LL_ADD(worker, pool->workers);
	}
	//监控线程
	if((pthread_create(&pool->monitorTid, NULL, monitorThread, pool)) != 0){
		perror("pthread_create");
		goto _exit;
	}
	return 0;

_exit:
	{
		nworker *w = NULL;
		for(w = pool->workers; w != NULL; w = w->next){
			w->terminate = 1;
		}
		return -1;

	}
	
}
//线程销毁
int nThreadPoolDestroy(nThreadPool *pool)
{
	//将所有的工作线程置为终止工作状态,线程回调函数中会自己释放各个节点
	nworker *w = NULL;
	for(w = pool->workers; w != NULL; w = w->next){
		w->terminate = 1;
	}
	//内存池监控线程退出标志
	pool->terminate = 1;
	
	 //将线程链表和任务链表置为空,任务是外部申请的,释放不属于内存池的责任
	pthread_mutex_lock(&pool->jobs_mtx);
	pool->workers = NULL;
	pool->jobs = NULL;

	//通知所有的线程
	pthread_cond_broadcast(&pool->jobs_cond);  //广播通知退出
	pthread_mutex_unlock(&pool->jobs_mtx);

	pthread_join(pool->monitorTid, NULL);
}

//task 添加到队列中
int nThreadPoolPushJobs(nThreadPool *pool, nJob *jobs)
{
	pthread_mutex_lock(&pool->jobs_mtx);

	LL_ADD(jobs, pool->jobs);
	
	pthread_cond_signal(&pool->jobs_cond); //唤醒
	
	pthread_mutex_unlock(&pool->jobs_mtx);
	
	return 0;
}


//API----提供给外界使用

/*

如何对线程池放缩？如何检测？如何释放？
manager中增加total(线程总数)，idele_thread;

free thread-----sum——thread < 20 %，释放线程
free thread-----sum_thread > 60% ，增加线程

*/
//如何对线程池放缩

#define KING_MAX_THREAD			10
#define KING_COUNTER_SIZE		1000

void *king_counter(nJob *job)
{

	int index = *(int*)job->user_data;

	printf("index : %d, selfid : %lu\n", index, pthread_self());
	sleep(2);
	free(job->user_data);
	free(job);
}

int main()
{
	nThreadPool pool;
	thread_pool_create(&pool, KING_MAX_THREAD);

	printf("create finish\n");
	int i = 0;
	for (i = 0;i < KING_COUNTER_SIZE;i ++) {
		nJob *job = (nJob *)malloc(sizeof(nJob));
		if (job == NULL) {
			perror("malloc");
			exit(1);
		}
		
		job->func = king_counter;
		job->user_data = malloc(sizeof(int));
		*(int*)job->user_data = i;

		nThreadPoolPushJobs(&pool, job);
		
	}
	getchar();
	nThreadPoolDestroy(&pool);
	return 0;
}


