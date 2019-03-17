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

#include <winsock2.h>
#include <gtest/gtest.h>
#include <windows.h>

#include "../src/EventObject.hpp"
#include "../src/packet.hpp"
#include "../src/SendQueue.hpp"

class SendQueueTest: public ::testing::Test {
	protected:
		EventObject event;
		SendQueue sq;
		
		SendQueueTest(): sq(event) {}
		~SendQueueTest() {}
		
		bool event_signalled()
		{
			return (WaitForSingleObject(event, 0) == WAIT_OBJECT_0);
		}
		
		uint32_t sqop_ptype(SendQueue::SendOp *sqop)
		{
			std::pair<const void*, size_t> sqop_data = sqop->get_data();
			
			PacketDeserialiser pd(sqop_data.first, sqop_data.second);
			return pd.packet_type();
		}
};

TEST_F(SendQueueTest, SendSingleLow)
{
	EXPECT_FALSE(event_signalled());
	
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(0), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	/* Event should signal once after calling send() */
	EXPECT_TRUE(event_signalled());
	EXPECT_FALSE(event_signalled());
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 0);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	EXPECT_EQ(sq.get_pending(), (SendQueue::SendOp*)(NULL));
}

TEST_F(SendQueueTest, SendMultiLow)
{
	EXPECT_FALSE(event_signalled());
	
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(1), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(2), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	/* Event should signal once after each batch of calls to send() */
	EXPECT_TRUE(event_signalled());
	EXPECT_FALSE(event_signalled());
	
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(0), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	/* Event should signal once after each batch of calls to send() */
	EXPECT_TRUE(event_signalled());
	EXPECT_FALSE(event_signalled());
	
	/* Check we get the messages in the right order. */
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 1);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 2);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 0);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	EXPECT_EQ(sq.get_pending(), (SendQueue::SendOp*)(NULL));
}

TEST_F(SendQueueTest, SendMultiMedium)
{
	EXPECT_FALSE(event_signalled());
	
	sq.send(SendQueue::SEND_PRI_MEDIUM, PacketSerialiser(1), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_MEDIUM, PacketSerialiser(2), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	/* Event should signal once after each batch of calls to send() */
	EXPECT_TRUE(event_signalled());
	EXPECT_FALSE(event_signalled());
	
	/* Check we get the messages in the right order. */
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 1);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 2);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	EXPECT_EQ(sq.get_pending(), (SendQueue::SendOp*)(NULL));
}

TEST_F(SendQueueTest, SendMultiHigh)
{
	EXPECT_FALSE(event_signalled());
	
	sq.send(SendQueue::SEND_PRI_HIGH, PacketSerialiser(1), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_HIGH, PacketSerialiser(2), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	/* Event should signal once after each batch of calls to send() */
	EXPECT_TRUE(event_signalled());
	EXPECT_FALSE(event_signalled());
	
	/* Check we get the messages in the right order. */
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 1);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 2);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	EXPECT_EQ(sq.get_pending(), (SendQueue::SendOp*)(NULL));
}

TEST_F(SendQueueTest, SendMultiPriorities)
{
	EXPECT_FALSE(event_signalled());
	
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(1), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_MEDIUM, PacketSerialiser(2), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_HIGH, PacketSerialiser(3), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(4), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_MEDIUM, PacketSerialiser(5), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_HIGH, PacketSerialiser(6), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	/* Event should signal once after each batch of calls to send() */
	EXPECT_TRUE(event_signalled());
	EXPECT_FALSE(event_signalled());
	
	/* Check we get the messages in the right order. */
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 3);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 6);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 2);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 5);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 1);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.get_pending(), sqop);
		
		EXPECT_EQ(sqop_ptype(sqop), 4);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	EXPECT_EQ(sq.get_pending(), (SendQueue::SendOp*)(NULL));
}

TEST_F(SendQueueTest, RemoveQueued)
{
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(1), NULL, 1,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_MEDIUM, PacketSerialiser(2), NULL, 2,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_HIGH, PacketSerialiser(3), NULL, 3,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	{
		SendQueue::SendOp *sqop = sq.remove_queued();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 3);
		
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.remove_queued();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 2);
		
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.remove_queued();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 1);
		
		delete sqop;
	}
	
	EXPECT_EQ(sq.remove_queued(), (SendQueue::SendOp*)(NULL));
	
	EXPECT_EQ(sq.get_pending(), (SendQueue::SendOp*)(NULL));
}

TEST_F(SendQueueTest, RemoveQueuedPending)
{
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(1), NULL, 1,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_MEDIUM, PacketSerialiser(2), NULL, 2,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_HIGH, PacketSerialiser(3), NULL, 3,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 3);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.remove_queued();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 2);
		
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.get_pending();
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 1);
		
		sq.pop_pending(sqop);
		delete sqop;
	}
	
	EXPECT_EQ(sq.remove_queued(), (SendQueue::SendOp*)(NULL));
	
	EXPECT_EQ(sq.get_pending(), (SendQueue::SendOp*)(NULL));
}

TEST_F(SendQueueTest, RemoveQueuedNoHandle)
{
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(1), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_MEDIUM, PacketSerialiser(2), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_HIGH, PacketSerialiser(3), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	EXPECT_EQ(sq.remove_queued(), (SendQueue::SendOp*)(NULL));
}

TEST_F(SendQueueTest, RemoveQueuedByHandle)
{
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(1), NULL, 1,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_MEDIUM, PacketSerialiser(2), NULL, 2,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_HIGH, PacketSerialiser(3), NULL, 3,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	{
		SendQueue::SendOp *sqop = sq.remove_queued_by_handle(1);
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 1);
		
		delete sqop;
		
		EXPECT_EQ(sq.remove_queued_by_handle(1), (SendQueue::SendOp*)(NULL));
	}
	
	{
		SendQueue::SendOp *sqop = sq.remove_queued_by_handle(2);
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 2);
		
		delete sqop;
		
		EXPECT_EQ(sq.remove_queued_by_handle(2), (SendQueue::SendOp*)(NULL));
	}
	
	{
		SendQueue::SendOp *sqop = sq.remove_queued_by_handle(3);
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 3);
		
		delete sqop;
		
		EXPECT_EQ(sq.remove_queued_by_handle(3), (SendQueue::SendOp*)(NULL));
	}
}

TEST_F(SendQueueTest, RemoveQueuedByPriority)
{
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(1), NULL, 1,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_MEDIUM, PacketSerialiser(2), NULL, 2,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_HIGH, PacketSerialiser(3), NULL, 3,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	{
		SendQueue::SendOp *sqop = sq.remove_queued_by_priority(SendQueue::SEND_PRI_LOW);
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.remove_queued_by_priority(SendQueue::SEND_PRI_LOW), (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 1);
		
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.remove_queued_by_priority(SendQueue::SEND_PRI_MEDIUM);
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.remove_queued_by_priority(SendQueue::SEND_PRI_MEDIUM), (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 2);
		
		delete sqop;
	}
	
	{
		SendQueue::SendOp *sqop = sq.remove_queued_by_priority(SendQueue::SEND_PRI_HIGH);
		ASSERT_NE(sqop, (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sq.remove_queued_by_priority(SendQueue::SEND_PRI_HIGH), (SendQueue::SendOp*)(NULL));
		
		EXPECT_EQ(sqop_ptype(sqop), 3);
		
		delete sqop;
	}
}

TEST_F(SendQueueTest, RemoveQueuedByPriorityNoHandle)
{
	sq.send(SendQueue::SEND_PRI_LOW, PacketSerialiser(1), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_MEDIUM, PacketSerialiser(2), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	sq.send(SendQueue::SEND_PRI_MEDIUM, PacketSerialiser(3), NULL,
		[](std::unique_lock<std::mutex> &l, HRESULT result) { return 0; });
	
	EXPECT_EQ(sq.remove_queued_by_priority(SendQueue::SEND_PRI_LOW),    (SendQueue::SendOp*)(NULL));
	EXPECT_EQ(sq.remove_queued_by_priority(SendQueue::SEND_PRI_MEDIUM), (SendQueue::SendOp*)(NULL));
	EXPECT_EQ(sq.remove_queued_by_priority(SendQueue::SEND_PRI_HIGH),   (SendQueue::SendOp*)(NULL));
}
