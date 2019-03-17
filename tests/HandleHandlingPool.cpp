/* DirectPlay Lite
 * Copyright (C) 2018 Daniel Collins
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

#include <winsock2.h>
#include <atomic>
#include <gtest/gtest.h>
#include <vector>
#include <windows.h>

#include "../src/EventObject.hpp"
#include "../src/HandleHandlingPool.hpp"

TEST(HandleHandlingPool, SingleThreadBasic)
{
	HandleHandlingPool pool(1, 32);
	
	EventObject e1(FALSE, TRUE);
	std::atomic<int> e1_counter(0);
	
	EventObject e2(FALSE, FALSE);
	std::atomic<int> e2_counter(0);
	
	EventObject e3(FALSE, FALSE);
	std::atomic<int> e3_counter(0);
	
	pool.add_handle(e1, [&e1, &e1_counter]() { if(++e1_counter < 4) { SetEvent(e1); } });
	pool.add_handle(e2, [&e2, &e2_counter]() { if(++e2_counter < 2) { SetEvent(e2); } });
	pool.add_handle(e3, [&e3_counter]() { ++e3_counter; });
	
	SetEvent(e2);
	
	Sleep(100);
	
	EXPECT_EQ(e1_counter, 4);
	EXPECT_EQ(e2_counter, 2);
	EXPECT_EQ(e3_counter, 0);
}

TEST(HandleHandlingPool, MultiThreadBasic)
{
	HandleHandlingPool pool(4, 32);
	
	EventObject e1(FALSE, TRUE);
	std::atomic<int> e1_counter(0);
	
	EventObject e2(FALSE, FALSE);
	std::atomic<int> e2_counter(0);
	
	EventObject e3(FALSE, FALSE);
	std::atomic<int> e3_counter(0);
	
	pool.add_handle(e1, [&e1, &e1_counter]() { if(++e1_counter < 4) { SetEvent(e1); } });
	pool.add_handle(e2, [&e2, &e2_counter]() { if(++e2_counter < 2) { SetEvent(e2); } });
	pool.add_handle(e3, [&e3_counter]() { ++e3_counter; });
	
	SetEvent(e2);
	
	Sleep(100);
	
	EXPECT_EQ(e1_counter, 4);
	EXPECT_EQ(e2_counter, 2);
	EXPECT_EQ(e3_counter, 0);
}

TEST(HandleHandlingPool, ThreadAssignment)
{
	HandleHandlingPool pool(4, 1);
	
	EventObject e1(TRUE, TRUE);
	std::atomic<int> e1_counter(0);
	
	EventObject e2(FALSE, TRUE);
	std::atomic<int> e2_counter(0);
	
	EventObject e3(FALSE, FALSE);
	std::atomic<int> e3_counter(0);
	
	std::mutex mutex;
	mutex.lock();
	
	pool.add_handle(e1, [&mutex, &e1_counter]() { ++e1_counter; mutex.lock(); mutex.unlock(); });
	pool.add_handle(e2, [&mutex, &e2, &e2_counter]() { if(++e2_counter < 20) { SetEvent(e2); } mutex.lock(); mutex.unlock(); });
	pool.add_handle(e3, [&e3_counter]() { ++e3_counter; });
	
	Sleep(100);
	
	/* We are holding the mutex, so the e1/e2 callbacks should have been invoked once by each
	 * worker thread, and still be sleeping until we released the mutex. The thread allocation
	 * for e3 should still be free.
	*/
	
	EXPECT_EQ(e1_counter, 4);
	EXPECT_EQ(e2_counter, 4);
	EXPECT_EQ(e3_counter, 0);
	
	SetEvent(e3);
	
	Sleep(100);
	
	/* Now e3 should have fired once and returned. */
	
	EXPECT_EQ(e1_counter, 4);
	EXPECT_EQ(e2_counter, 4);
	EXPECT_EQ(e3_counter, 1);
	
	ResetEvent(e1);
	mutex.unlock();
	
	Sleep(100);
	
	/* Since we released the mutex, the pending e1/e2 callbacks should return and the e2
	 * callback should keep signalling itself until it reaches 20 calls.
	*/
	
	EXPECT_EQ(e1_counter, 4);
	EXPECT_EQ(e2_counter, 20);
	EXPECT_EQ(e3_counter, 1);
}

TEST(HandleHandlingPool, RemoveHandle)
{
	HandleHandlingPool pool(4, 1);
	
	EventObject e1(FALSE, FALSE);
	std::atomic<int> e1_counter(0);
	
	EventObject e2(FALSE, FALSE);
	std::atomic<int> e2_counter(0);
	
	EventObject e3(FALSE, FALSE);
	std::atomic<int> e3_counter(0);
	
	pool.add_handle(e1, [&e1_counter]() { ++e1_counter; });
	pool.add_handle(e2, [&e2_counter]() { ++e2_counter; });
	pool.add_handle(e3, [&e3_counter]() { ++e3_counter; });
	
	SetEvent(e1);
	SetEvent(e2);
	SetEvent(e3);
	
	Sleep(100);
	
	EXPECT_EQ(e1_counter, 1);
	EXPECT_EQ(e2_counter, 1);
	EXPECT_EQ(e3_counter, 1);
	
	pool.remove_handle(e2);
	
	SetEvent(e1);
	SetEvent(e2);
	SetEvent(e3);
	
	Sleep(100);
	
	EXPECT_EQ(e1_counter, 2);
	EXPECT_EQ(e2_counter, 1);
	EXPECT_EQ(e3_counter, 2);
}

TEST(HandleHandlingPool, Stress)
{
	/* Stress test to see if we can trigger a race/crash - 4 groups of 16 handles, each with 8
	 * worker threads, and each handle signalling 10,000 times.
	*/
	
	std::vector<EventObject> events(64);
	std::vector< std::atomic<int> > counters(64);
	
	HandleHandlingPool pool(8, 16);
	
	for(int i = 0; i < 64; ++i)
	{
		pool.add_handle(events[i], [i, &counters, &events]() { if(++counters[i] < 10000) { SetEvent(events[i]); } });
		SetEvent(events[i]);
	}
	
	/* Is 5s enough to handle 640,000 events across 32 threads? Probably! */
	Sleep(5000);
	
	for(int i = 0; i < 64; ++i)
	{
		EXPECT_EQ(counters[i], 10000);
	}
}
