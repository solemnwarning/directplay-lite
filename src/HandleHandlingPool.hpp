/* DirectPlay Lite
 * Copyright (C) 2018 Daniel Collins <solemnwarning@solemnwarning.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef DPLITE_HANDLEHANDLINGPOOL_HPP
#define DPLITE_HANDLEHANDLINGPOOL_HPP

#include <winsock2.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <windows.h>

/* This class maintains a pool of threads to wait on HANDLEs and invoke callback functors when the
 * HANDLEs become signalled.
 *
 * For up to every max_handles_per_pool HANDLEs registered, threads_per_pool worker threads will be
 * created to wait on them. Each block of HANDLEs will be assigned to their own pool of threads but
 * may move between them when handles are removed.
 *
 * Handles may be added or removed at any point, although this is not a heavily optimised path and
 * will temporarily block all worker threads from waiting on their handles.
 *
 * Note that the same callback may be invoked in multiple threads concurrently if the linked HANDLE
 * is signalled multiple times in quick sucession or is a manual reset event. Ensure your callbacks
 * can handle this and do not block, as this will prevent other HANDLEs managed by the same thread
 * from being invoked.
*/

class HandleHandlingPool
{
	private:
		struct Worker
		{
			const size_t base_index;
			std::thread thread;
			
			Worker(size_t base_index);
		};
		
		const size_t threads_per_pool;
		const size_t max_handles_per_pool;
		
		/* spin_workers is a MANUAL RESET event object, we set this to signalled whenever
		 * we need all the worker threads to exit their WaitForMultipleObjects() calls.
		*/
		
		HANDLE spin_workers;
		std::atomic<bool> stopping;
		
		/* Array of handles to wait for, and the callbacks to invoke when they become
		 * signalled.
		 *
		 * These are interlaced with spin_workers every multiple of max_handles_per_pool
		 * elements, so each thread may pass a range directly into WaitForMultipleObjects()
		 * and will be woken by WAIT_OBJECT_0 when we signal spin_workers.
		 *
		 * Slots in callbacks which correspond to spin_workers have functors which must
		 * never be called to simplify the implementation.
		*/
		
		std::vector<HANDLE> handles;
		std::vector< std::function<void()> > callbacks;
		
		/* This shared_mutex protects access to handles/callbacks.
		 *
		 * Multiple workers may hold it, and will do so before they start waiting for
		 * events until they enter a callback method.
		 *
		 * One thread may hold it exclusively in order to add or remove a handle, this
		 * will block any workers from waiting for events.
		*/
		
		std::shared_mutex wait_lock;
		
		/* pending_writer ensures worker threads cannot starve writers which are trying to
		 * exclusively take wait_lock.
		 *
		 * When a thread wants to take the lock exclusively, they:
		 *
		 * 1) Claim pending_writer_lock
		 * 2) Set pending_writer
		 * 3) Set spin_workers to signalled
		 * 4) Exclusively lock wait_lock
		 *
		 * Then, any worker threads will do the following:
		 *
		 * 4) If currently waiting, return from WaitForMultipleObjects() and then release
		 *    its shared lock on wait_lock.
		 *
		 * 5) Check pending_writer, if true, wait on pending_writer_cv for it to be false.
		 *
		 *    This doesn't really need to be mutex protected, and there is still a possible
		 *    race between the worker releasing pending_writer_lock and re-acquiring
		 *    wait_lock and another thread attempting to lock it exclusively again, but
		 *    then it will just spin around the loop again due to spin_workers.
		 *
		 * Once all workers have relinquished wait_lock, the thread which has
		 * pending_writer_lock will do the following:
		 *
		 * 6) Reset spin_workers to non-signalled
		 * 7) Clear pending_writer
		 * 8) Release pending_writer_lock
		 *
		 * At this point, the writer is free to mess around with the handles, before
		 * releasing wait_lock and signalling pending_writer_cv. All worker threads will
		 * now be eligible to wake up and resume operations.
		*/
		
		std::atomic<bool>       pending_writer;
		std::mutex              pending_writer_lock;
		std::condition_variable pending_writer_cv;
		
		/* active_workers contains a pointer to the Worker object for each running or
		 * starting worker thread.
		 *
		 * join_worker, if joinable, is a handle to the last worker thread which exited.
		 *
		 * Worker threads are responsible for removing themselves from the active_workers
		 * set when they exit and placing themselves into join_worker. If join_worker is
		 * already set to a valid thread, the new exiting thread must wait on it. The final
		 * thread is joined by our destructor.
		 *
		 * This ensures that we can only ever have one zombie thread at a time and all
		 * threads have been joined before our destructor returns.
		 *
		 * workers_lock serialises access to active_workers AND join_worker; any thread
		 * must lock it before performing ANY operation on either.
		 *
		 * workers_cv is signalled by worker threads when they exit, after they have placed
		 * themselves into join_worker and are about to exit. This is used by the destructor
		 * to wait for zero active_workers.
		*/
		
		std::set<Worker*>       active_workers;
		std::thread             join_worker;
		std::mutex              workers_lock;
		std::condition_variable workers_cv;
		
		void worker_main(HandleHandlingPool::Worker *w);
		void worker_exit(HandleHandlingPool::Worker *w);
		
	public:
		HandleHandlingPool(size_t threads_per_pool, size_t max_handles_per_pool);
		~HandleHandlingPool();
		
		void add_handle(HANDLE handle, const std::function<void()> &callback);
		void remove_handle(HANDLE handle);
};

#endif /* !DPLITE_HANDLEHANDLINGPOOL_HPP */
