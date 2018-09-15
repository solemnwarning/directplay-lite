#include <algorithm>
#include <exception>
#include <stdexcept>
#include <stdlib.h>

#include "HandleHandlingPool.hpp"

HandleHandlingPool::HandleHandlingPool(size_t threads_per_pool, size_t max_handles_per_pool):
	threads_per_pool(threads_per_pool),
	max_handles_per_pool(max_handles_per_pool + 1),
	stopping(false)
{
	if(threads_per_pool < 1)
	{
		throw std::invalid_argument("threads_per_pool must be >= 1");
	}
	
	if(max_handles_per_pool < 1 || max_handles_per_pool >= MAXIMUM_WAIT_OBJECTS)
	{
		throw std::invalid_argument("max_handles_per_pool must be >= 1 and < MAXIMUM_WAIT_OBJECTS");
	}
	
	spin_workers = CreateEvent(NULL, TRUE, FALSE, NULL);
	if(spin_workers == NULL)
	{
		throw std::runtime_error("Unable to create event object");
	}
}

HandleHandlingPool::~HandleHandlingPool()
{
	/* Signal all active workers to stop and wait for any to exit. */
	
	stopping = true;
	SetEvent(spin_workers);
	
	std::unique_lock<std::mutex> wo_l(workers_lock);
	workers_cv.wait(wo_l, [this]() { return active_workers.empty(); });
	
	/* Reap the last thread which exited, if any were spawned. */
	
	if(join_worker.joinable())
	{
		join_worker.join();
	}
	
	CloseHandle(spin_workers);
}

void HandleHandlingPool::add_handle(HANDLE handle, const std::function<void()> &callback)
{
	/* See HandleHandlingPool.hpp for an explanation of this sequence. */
	
	std::unique_lock<std::mutex> pwl(pending_writer_lock);
	
	pending_writer = true;
	SetEvent(spin_workers);
	
	std::unique_lock<std::shared_mutex> wal(wait_lock);
	
	ResetEvent(spin_workers);
	pending_writer = false;
	
	pwl.unlock();
	
	/* Notify early and move any waiting workers up against wait_lock in case one of the below
	 * operations fails, else they may be left deadlocked.
	*/
	pending_writer_cv.notify_all();
	
	if((handles.size() % max_handles_per_pool) == 0)
	{
		/* There aren't any free slots for this handle in the currently running worker
		 * threads, start a new block beginning with spin_workers.
		*/
		
		size_t base_index = handles.size();
		
		handles.push_back(spin_workers);
		try {
			callbacks.push_back([](){ abort(); }); /* Callback should never be executed. */
			
			handles.push_back(handle);
			try {
				callbacks.push_back(callback);
			}
			catch(const std::exception &e)
			{
				handles.pop_back();
				throw e;
			}
		}
		catch(const std::exception &e)
		{
			handles.pop_back();
			throw e;
		}
		
		/* Calculate how many threads we need to spawn for there to be threads_per_pool
		 * workers for this pool.
		 *
		 * This may be less than threads_per_pool, if the block we just created previously
		 * existed, then some handles were removed causing it to go away - threads will exit
		 * once they detect they have nothing to wait for, but we may also add more handles
		 * before they catch up.
		 *
		 * Workers check if they have anything to do and remove themselves from
		 * active_workers while holding wait_lock, so there is no race between us counting
		 * the workers and them going away.
		*/
		
		std::unique_lock<std::mutex> wo_l(workers_lock);
		
		size_t threads_to_spawn = threads_per_pool;
		
		for(auto w = active_workers.begin(); w != active_workers.end(); w++)
		{
			if((*w)->base_index == base_index)
			{
				--threads_to_spawn;
			}
		}
		
		/* Spawn the new worker threads.
		 *
		 * We need to create the worker data on the heap so that:
		 *
		 * a) We can pass a reference to it into the worker main so that it may remove
		 *    itself from active_workers when the time comes.
		 *
		 * b) The reference in active_workers itself is const, so that thread may be
		 *    changed by the thread starting/exiting.
		 *
		 * The thread won't attempt to do anything (e.g. exit) until after acquiring
		 * wait_lock, so it won't attempt to do anything with its thread handle before it
		 * is done being initialised.
		*/
		
		for(size_t i = 0; i < threads_to_spawn; ++i)
		{
			Worker *w = new Worker(base_index);
			
			try {
				active_workers.insert(w);
				
				try {
					w->thread = std::thread(&HandleHandlingPool::worker_main, this, w);
				}
				catch(const std::exception &e)
				{
					active_workers.erase(w);
					throw e;
				}
			}
			catch(const std::exception &e)
			{
				delete w;
				
				if(i == 0)
				{
					/* This is the first worker we tried spawning, fail the
					 * whole operation.
					*/
					handles.pop_back(); callbacks.pop_back();
					handles.pop_back(); callbacks.pop_back();
					
					throw e;
				}
			}
		}
	}
	else{
		handles.push_back(handle);
		
		try {
			callbacks.push_back(callback);
		}
		catch(const std::exception &e)
		{
			handles.pop_back();
			throw e;
		}
	}
}

void HandleHandlingPool::remove_handle(HANDLE handle)
{
	/* See HandleHandlingPool.hpp for an explanation of this sequence. */
	
	std::unique_lock<std::mutex> pwl(pending_writer_lock);
	
	pending_writer = true;
	SetEvent(spin_workers);
	
	std::unique_lock<std::shared_mutex> wal(wait_lock);
	
	ResetEvent(spin_workers);
	pending_writer = false;
	
	pwl.unlock();
	
	/* Scan handles to find the index of the handle to be removed. */
	
	size_t remove_index = 0;
	while(remove_index < handles.size() && handles[remove_index] != handle)
	{
		++remove_index;
	}
	
	if(remove_index >= handles.size())
	{
		/* Couldn't find the handle. */
		return;
	}
	
	/* If the last handle is spin_workers, then the last call to remove_handle() removed the
	 * last handle in the last block. Now we remove it and the worker threads for that block
	 * will exit when they wake up.
	 *
	 * Doing it this way around means we destroy worker threads after removing its last handle
	 * and then another handle, reducing thrash if a single handle is added/removed at the
	 * boundary. Downside is we may keep one group of idle threads around for no reason.
	*/
	
	if(handles.back() == spin_workers)
	{
		handles.pop_back();
		callbacks.pop_back();
	}
	
	/* Replace it with the last handle. */
	
	handles[remove_index] = handles.back();
	handles.pop_back();
	
	callbacks[remove_index] = callbacks.back();
	callbacks.pop_back();
	
	pending_writer_cv.notify_all();
}

void HandleHandlingPool::worker_main(HandleHandlingPool::Worker *w)
{
	while(1)
	{
		if(pending_writer)
		{
			std::unique_lock<std::mutex> pwl(pending_writer_lock);
			pending_writer_cv.wait(pwl, [this]() { return !pending_writer; });
		}
		
		std::shared_lock<std::shared_mutex> l(wait_lock);
		
		if(handles.size() <= w->base_index)
		{
			/* No handles to wait on. Exit. */
			worker_exit(w);
			return;
		}
		
		/* Number of handles to wait for. We wait from base_index to the end of the handles
		 * array or the end of our block, whichever is closest.
		*/
		size_t num_handles = std::min((handles.size() - w->base_index), max_handles_per_pool);
		
		DWORD wait_res = WaitForMultipleObjects(num_handles, &(handles[w->base_index]), FALSE, INFINITE);
		
		if(stopping)
		{
			worker_exit(w);
			return;
		}
		
		if(wait_res >= (WAIT_OBJECT_0 + 1) && wait_res < (WAIT_OBJECT_0 + num_handles))
		{
			/* Take a copy of the callback functor so we can release all of our locks
			 * without worrying about it disappearing from under itself if handles are
			 * added/removed while its executing.
			*/
			size_t wait_index = (w->base_index + wait_res) - WAIT_OBJECT_0;
			std::function<void()> callback = callbacks[wait_index];
			
			l.unlock();
			
			callback();
		}
		else if(wait_res == WAIT_FAILED)
		{
			/* Some system error while waiting... invalid handle?
			 * Only thing we can do is go quietly. Or maybe not so quietly... abort?
			*/
			worker_exit(w);
			return;
		}
	}
}

/* Exit a worker thread.
 *
 * This MUST only be called by the worker thread which is about to exit, which
 * MUST exit as soon as this method returns.
*/
void HandleHandlingPool::worker_exit(HandleHandlingPool::Worker *w)
{
	std::unique_lock<std::mutex> wo_l(workers_lock);
	
	if(join_worker.joinable())
	{
		/* Only one zombie thread is allowed at a time. If one exists, we must reap it
		 * before taking its place.
		*/
		join_worker.join();
	}
	
	/* MSVC doesn't like assignment here for some reason. */
	join_worker.swap(w->thread);
	
	active_workers.erase(w);
	delete w;
	
	wo_l.unlock();
	workers_cv.notify_one();
	
	/* This thread is now ready to be reaped by the next thread to exit, or the destructor, if
	 * we are the last one.
	*/
}

HandleHandlingPool::Worker::Worker(size_t base_index):
	base_index(base_index) {}
