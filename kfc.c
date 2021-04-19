#include <assert.h>
#include <sys/types.h>
#include <stdlib.h>

#include "queue.h"
#include "kfc.h"

static int inited = 0;
int numThreads;
static int currentThread;
static KFCBlock threadList[KFC_MAX_THREADS];
static queue_t threadQueue;

/**
 * Joel Armstrong
 * Initializes the kfc library.  Programs are required to call this function
 * before they may use anything else in the library's public interface.
 *
 * @param kthreads    Number of kernel threads (pthreads) to allocate
 * @param quantum_us  Preemption timeslice in microseconds, or 0 for cooperative
 *                    scheduling
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_init(int kthreads, int quantum_us)
{
	assert(!inited);


	for (int i = 0; i < KFC_MAX_THREADS; ++ i )
	{
		threadList[i].active = 0;
		threadList[i].tid = i;
	}

	queue_init(&threadQueue);
	currentThread = 0;
	numThreads = 1;
	threadList[0].active =1;

	getcontext(&threadList[0].context);
	threadList[0].context.uc_link = 0;
	threadList[0].context.uc_stack.ss_sp = malloc(KFC_DEF_STACK_SIZE);
	threadList[0].context.uc_stack.ss_size = KFC_DEF_STACK_SIZE;
	threadList[0].context.uc_stack.ss_flags = 0;
	makecontext(&threadList[0].context, (void (*)(void)) kfc_scheduler, 0);

	inited = 1;
	return 0;

}


int kfc_scheduler(void) {
	//currentThread = 0;
	while(queue_size(&threadQueue) > 0){
		KFCBlock *next =  queue_dequeue(&threadQueue);
		DPRINTF("scheduling %d, queue size: %d\n", next->tid, queue_size(&threadQueue));
		currentThread = (int) next->tid;
		setcontext(&threadList[currentThread].context);
	}
	return 0;
}
/**
 * Cleans up any resources which were allocated by kfc_init.  You may assume
 * that this function is called only from the main thread, that any other
 * threads have terminated and been joined, and that threading will not be
 * needed again.  (In other words, just clean up and don't worry about the
 * consequences.)
 *
 * I won't test this function, since it wasn't part of the original assignment;
 * it is provided as a convenience to you if you are using Valgrind to test
 * (which I heartily encourage).
 */
void
kfc_teardown(void)
{
	assert(inited);

	inited = 0;
}

/**
 * Creates a new user thread which executes the provided function concurrently.
 * It is left up to the implementation to decide whether the calling thread
 * continues to execute or the new thread takes over immediately.
 *
 * @param ptid[out]   Pointer to a tid_t variable in which to store the new
 *                    thread's ID
 * @param start_func  Thread main function
 * @param arg         Argument to be passed to the thread main function
 * @param stack_base  Location of the thread's stack if already allocated, or
 *                    NULL if requesting that the library allocate it
 *                    dynamically
 * @param stack_size  Size (in bytes) of the thread's stack, or 0 to use the
 *                    default thread stack size KFC_DEF_STACK_SIZE
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_create(tid_t *ptid, void *(*start_func)(void *), void *arg,
		caddr_t stack_base, size_t stack_size)
{
	assert(inited);
	if (numThreads ==  KFC_MAX_THREADS) return 1;
	/* The "main" execution context */
	/* find a valid inactive tid */
	for(int i = 0; i < KFC_MAX_THREADS; i++){
		if(threadList[i].active == 0){
			*ptid = i;
			threadList[i].active = 1;
			numThreads++;
			break;
		}
	}

	getcontext(&threadList[*ptid].context);
	if(stack_size == 0){
		stack_size = KFC_DEF_STACK_SIZE;
	}
	if(stack_base == NULL){
		stack_base = malloc(stack_size);
	}

	/* Set the context to a newly allocated stack */
	threadList[*ptid].stack = stack_base;
	threadList[*ptid].tid = *ptid;
	threadList[*ptid].context.uc_link = &threadList[0].context;
	threadList[*ptid].context.uc_stack.ss_sp = threadList[*ptid].stack;
	threadList[*ptid].context.uc_stack.ss_size = stack_size;
	threadList[*ptid].context.uc_stack.ss_flags = 0;

	if ( threadList[*ptid].stack == 0 )
	{
		DPRINTF( "Error: Could not allocate stack." );
		return 1;
	}

	/* Create the context. */
	DPRINTF("thread %d created\n", *ptid);
	makecontext(&threadList[*ptid].context, (void (*)(void)) start_func, 1, arg);
	queue_enqueue(&threadQueue, &threadList[*ptid]);
	return 0;
}

/**
 * Exits the calling thread.  This should be the same thing that happens when
 * the thread's start_func returns.
 *
 * @param ret  Return value from the thread
 */
void
kfc_exit(void *ret)
{
	assert(inited);
}

/**
 * Waits for the thread specified by tid to terminate, retrieving that threads
 * return value.  Returns immediately if the target thread has already
 * terminated, otherwise blocks.  Attempting to join a thread which already has
 * another thread waiting to join it, or attempting to join a thread which has
 * already been joined, results in undefined behavior.
 *
 * @param pret[out]  Pointer to a void * in which the thread's return value from
 *                   kfc_exit should be stored, or NULL if the caller does not
 *                   care.
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_join(tid_t tid, void **pret)
{
	assert(inited);

	return 0;
}

/**
 * Returns a small integer which identifies the calling thread.
 *
 * @return Thread ID of the currently executing thread
 */
tid_t
kfc_self(void)
{
	assert(inited);
	DPRINTF("Self is %d \n", currentThread);
	return currentThread;
}

/**
 * Causes the calling thread to yield the processor voluntarily.  This may
 * result in another thread being scheduled, but it does not preclude the
 * possibility of the same thread continuing if re-chosen by the scheduling
 * algorithm.
 */
void
kfc_yield(void)
{
	assert(inited);

	queue_enqueue(&threadQueue, &threadList[currentThread]);
	DPRINTF("%d yielding to scheduler\n", currentThread);
	kfc_scheduler();
	return;
}

/**
 * Initializes a user-level counting semaphore with a specific value.
 *
 * @param sem    Pointer to the semaphore to be initialized
 * @param value  Initial value for the semaphore's counter
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_sem_init(kfc_sem_t *sem, int value)
{
	assert(inited);
	return 0;
}

/**
 * Increments the value of the semaphore.  This operation is also known as
 * up, signal, release, and V (Dutch verhoog, "increase").
 *
 * @param sem  Pointer to the semaphore which the thread is releasing
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_sem_post(kfc_sem_t *sem)
{
	assert(inited);
	return 0;
}

/**
 * Attempts to decrement the value of the semaphore.  This operation is also
 * known as down, acquire, and P (Dutch probeer, "try").  This operation should
 * block when the counter is not above 0.
 *
 * @param sem  Pointer to the semaphore which the thread wishes to acquire
 *
 * @return 0 if successful, nonzero on failure
 */
int
kfc_sem_wait(kfc_sem_t *sem)
{
	assert(inited);
	return 0;
}

/**
 * Frees any resources associated with a semaphore.  Destroying a semaphore on
 * which threads are waiting results in undefined behavior.
 *
 * @param sem  Pointer to the semaphore to be destroyed
 */
void
kfc_sem_destroy(kfc_sem_t *sem)
{
	assert(inited);
}
