#include <gtest/gtest.h>

#include "../src/packet.hpp"

TEST(PacketSerialiser, Empty)
{
	PacketSerialiser p(0xAA);
	
	std::pair<const void*, size_t> raw = p.raw_packet();
	
	const unsigned char EXPECT[] = {
		0xAA, 0x00, 0x00, 0x00,  /* type */
		0x00, 0x00, 0x00, 0x00,  /* value_length */
	};
	
	std::vector<unsigned char> got((unsigned char*)(raw.first), (unsigned char*)(raw.first) + raw.second);
	std::vector<unsigned char> expect(EXPECT, EXPECT + sizeof(EXPECT));
	
	ASSERT_EQ(got, expect);
}

TEST(PacketSerialiser, Null)
{
	PacketSerialiser p(0xBB);
	p.append_null();
	
	std::pair<const void*, size_t> raw = p.raw_packet();
	
	const unsigned char EXPECT[] = {
		0xBB, 0x00, 0x00, 0x00,  /* type */
		0x08, 0x00, 0x00, 0x00,  /* value_length */
		
		0x00, 0x00, 0x00, 0x00,  /* type */
		0x00, 0x00, 0x00, 0x00,  /* value_length */
	};
	
	std::vector<unsigned char> got((unsigned char*)(raw.first), (unsigned char*)(raw.first) + raw.second);
	std::vector<unsigned char> expect(EXPECT, EXPECT + sizeof(EXPECT));
	
	ASSERT_EQ(got, expect);
}

TEST(PacketSerialiser, DWORD)
{
	PacketSerialiser p(0xAABBCCDD);
	p.append_dword(0xFFEEDDCC);
	
	std::pair<const void*, size_t> raw = p.raw_packet();
	
	const unsigned char EXPECT[] = {
		0xDD, 0xCC, 0xBB, 0xAA,  /* type */
		0x0C, 0x00, 0x00, 0x00,  /* value_length */
		
		0x01, 0x00, 0x00, 0x00,  /* type */
		0x04, 0x00, 0x00, 0x00,  /* value_length */
		0xCC, 0xDD, 0xEE, 0xFF,  /* value */
	};
	
	std::vector<unsigned char> got((unsigned char*)(raw.first), (unsigned char*)(raw.first) + raw.second);
	std::vector<unsigned char> expect(EXPECT, EXPECT + sizeof(EXPECT));
	
	ASSERT_EQ(got, expect);
}

TEST(PacketSerialiser, Data)
{
	PacketSerialiser p(0x1234);
	
	const unsigned char DATA[] = {
		0x01, 0x23, 0x45, 0x67,
		0x89, 0xAB, 0xCD, 0xEF,
	};
	
	p.append_data(DATA, sizeof(DATA));
	
	std::pair<const void*, size_t> raw = p.raw_packet();
	
	const unsigned char EXPECT[] = {
		0x34, 0x12, 0x00, 0x00,  /* type */
		0x10, 0x00, 0x00, 0x00,  /* value_length */
		
		0x02, 0x00, 0x00, 0x00,  /* type */
		0x08, 0x00, 0x00, 0x00,  /* value_length */
		0x01, 0x23, 0x45, 0x67,  /* value */
		0x89, 0xAB, 0xCD, 0xEF,
	};
	
	std::vector<unsigned char> got((unsigned char*)(raw.first), (unsigned char*)(raw.first) + raw.second);
	std::vector<unsigned char> expect(EXPECT, EXPECT + sizeof(EXPECT));
	
	ASSERT_EQ(got, expect);
}

TEST(PacketSerialiser, WString)
{
	PacketSerialiser p(0x1234);
	
	p.append_wstring(L"Hello, I'm Gabe Newell");
	
	std::pair<const void*, size_t> raw = p.raw_packet();
	
	const unsigned char EXPECT[] = {
		0x34, 0x12, 0x00, 0x00,  /* type */
		0x34, 0x00, 0x00, 0x00,  /* value_length (52) */
		
		0x03, 0x00, 0x00, 0x00,  /* type */
		0x2C, 0x00, 0x00, 0x00,  /* value_length (44) */
		0x48, 0x00, 0x65, 0x00,  /* value */
		0x6C, 0x00, 0x6C, 0x00,
		0x6F, 0x00, 0x2C, 0x00,
		0x20, 0x00, 0x49, 0x00,
		0x27, 0x00, 0x6D, 0x00,
		0x20, 0x00, 0x47, 0x00,
		0x61, 0x00, 0x62, 0x00,
		0x65, 0x00, 0x20, 0x00,
		0x4E, 0x00, 0x65, 0x00,
		0x77, 0x00, 0x65, 0x00,
		0x6C, 0x00, 0x6C, 0x00
	};
	
	std::vector<unsigned char> got((unsigned char*)(raw.first), (unsigned char*)(raw.first) + raw.second);
	std::vector<unsigned char> expect(EXPECT, EXPECT + sizeof(EXPECT));
	
	ASSERT_EQ(got, expect);
}

TEST(PacketSerialiser, NullDWORDDataWString)
{
	PacketSerialiser p(0x1234);
	
	p.append_null();
	p.append_dword(0xEDFE);
	
	const unsigned char DATA[] = { 0x01, 0x23, 0x45, 0x67, 0x89 };
	p.append_data(DATA, sizeof(DATA));
	
	p.append_wstring(L"WStr");
	
	std::pair<const void*, size_t> raw = p.raw_packet();
	
	const unsigned char EXPECT[] = {
		0x34, 0x12, 0x00, 0x00,  /* type */
		0x31, 0x00, 0x00, 0x00,  /* value_length (49) */
		
		0x00, 0x00, 0x00, 0x00,  /* type */
		0x00, 0x00, 0x00, 0x00,  /* value_length */
		
		0x01, 0x00, 0x00, 0x00,  /* type */
		0x04, 0x00, 0x00, 0x00,  /* value_length */
		0xFE, 0xED, 0x00, 0x00,  /* value */
		
		0x02, 0x00, 0x00, 0x00,  /* type */
		0x05, 0x00, 0x00, 0x00,  /* value_length */
		0x01, 0x23, 0x45, 0x67,  /* value */
		0x89,
		
		0x03, 0x00, 0x00, 0x00,  /* type */
		0x08, 0x00, 0x00, 0x00,  /* value_length */
		0x57, 0x00, 0x53, 0x00,  /* value */
		0x74, 0x00, 0x72, 0x00,
	};
	
	std::vector<unsigned char> got((unsigned char*)(raw.first), (unsigned char*)(raw.first) + raw.second);
	std::vector<unsigned char> expect(EXPECT, EXPECT + sizeof(EXPECT));
	
	ASSERT_EQ(got, expect);
}
