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

#include <gtest/gtest.h>

#include "../src/packet.hpp"

class PacketDeserialiserTest: public ::testing::Test {
	protected:
		PacketDeserialiser *pd;
		
		PacketDeserialiserTest(): pd(NULL) {}
		
		virtual ~PacketDeserialiserTest()
		{
			delete pd;
		}
};

class PacketDeserialiserEmpty: public PacketDeserialiserTest {
	protected:
		virtual void SetUp() override
		{
			static const unsigned char RAW[] = {
				0x01, 0x00, 0x00, 0x00,  /* type */
				0x00, 0x00, 0x00, 0x00,  /* value_length */
			};
			
			ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
		}
};

TEST_F(PacketDeserialiserEmpty, Type)
{
	EXPECT_EQ(pd->packet_type(), (uint32_t)(1));
}

TEST_F(PacketDeserialiserEmpty, NumFields)
{
	EXPECT_EQ(pd->num_fields(), (size_t)(0));
}

TEST_F(PacketDeserialiserEmpty, IsNull)
{
	EXPECT_THROW({ pd->is_null(0); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserEmpty, GetDWORD)
{
	EXPECT_THROW({ pd->get_dword(0); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserEmpty, GetData)
{
	EXPECT_THROW({ pd->get_data(0); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserEmpty, GetWString)
{
	EXPECT_THROW({ pd->get_wstring(0); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserEmpty, GetGUID)
{
	EXPECT_THROW({ pd->get_guid(0); }, PacketDeserialiser::Error::MissingField);
}

class PacketDeserialiserNull: public PacketDeserialiserTest {
	protected:
		virtual void SetUp() override
		{
			static const unsigned char RAW[] = {
				0x02, 0x00, 0x00, 0x00,  /* type */
				0x08, 0x00, 0x00, 0x00,  /* value_length */
				
				0x00, 0x00, 0x00, 0x00,  /* type */
				0x00, 0x00, 0x00, 0x00,  /* value_length */
			};
			
			ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
		}
};

TEST_F(PacketDeserialiserNull, Type)
{
	EXPECT_EQ(pd->packet_type(), (uint32_t)(2));
}

TEST_F(PacketDeserialiserNull, NumFields)
{
	EXPECT_EQ(pd->num_fields(), (size_t)(1));
}

TEST_F(PacketDeserialiserNull, IsNull)
{
	EXPECT_NO_THROW({ EXPECT_EQ(pd->is_null(0), true); });
	EXPECT_THROW({ pd->is_null(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserNull, GetDWORD)
{
	EXPECT_THROW({ pd->get_dword(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_dword(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserNull, GetData)
{
	EXPECT_THROW({ pd->get_data(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_data(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserNull, GetWString)
{
	EXPECT_THROW({ pd->get_wstring(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_wstring(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserNull, GetGUID)
{
	EXPECT_THROW({ pd->get_guid(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_guid(1); }, PacketDeserialiser::Error::MissingField);
}

class PacketDeserialiserDWORD: public PacketDeserialiserTest {
	protected:
		virtual void SetUp() override
		{
			static const unsigned char RAW[] = {
				0x03, 0x00, 0x00, 0x00,  /* type */
				0x0C, 0x00, 0x00, 0x00,  /* value_length */
				
				0x01, 0x00, 0x00, 0x00,  /* type */
				0x04, 0x00, 0x00, 0x00,  /* value_length */
				0x01, 0x23, 0x45, 0x67,  /* value */
			};
			
			ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
		}
};

TEST_F(PacketDeserialiserDWORD, Type)
{
	EXPECT_EQ(pd->packet_type(), (uint32_t)(3));
}

TEST_F(PacketDeserialiserDWORD, NumFields)
{
	EXPECT_EQ(pd->num_fields(), (size_t)(1));
}

TEST_F(PacketDeserialiserDWORD, IsNull)
{
	EXPECT_NO_THROW({ EXPECT_EQ(pd->is_null(0), false); });
	EXPECT_THROW({ pd->is_null(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserDWORD, GetDWORD)
{
	EXPECT_NO_THROW({ EXPECT_EQ(pd->get_dword(0), (DWORD)(0x67452301)); });
	EXPECT_THROW({ pd->get_dword(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserDWORD, GetData)
{
	EXPECT_THROW({ pd->get_data(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_data(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserDWORD, GetWString)
{
	EXPECT_THROW({ pd->get_wstring(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_wstring(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserDWORD, GetGUID)
{
	EXPECT_THROW({ pd->get_guid(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_guid(1); }, PacketDeserialiser::Error::MissingField);
}

class PacketDeserialiserData: public PacketDeserialiserTest {
	protected:
		virtual void SetUp() override
		{
			static const unsigned char RAW[] = {
				0x04, 0x00, 0x00, 0x00,  /* type */
				0x0E, 0x00, 0x00, 0x00,  /* value_length */
				
				0x02, 0x00, 0x00, 0x00,  /* type */
				0x06, 0x00, 0x00, 0x00,  /* value_length */
				0xFE, 0xED, 0xBE, 0xEF,  /* value */
				0xAA, 0xAA,
			};
			
			ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
		}
};

TEST_F(PacketDeserialiserData, Type)
{
	EXPECT_EQ(pd->packet_type(), (uint32_t)(4));
}

TEST_F(PacketDeserialiserData, NumFields)
{
	EXPECT_EQ(pd->num_fields(), (size_t)(1));
}

TEST_F(PacketDeserialiserData, IsNull)
{
	EXPECT_NO_THROW({ EXPECT_EQ(pd->is_null(0), false); });
	EXPECT_THROW({ pd->is_null(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserData, GetDWORD)
{
	EXPECT_THROW({ pd->get_dword(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_dword(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserData, GetData)
{
	const unsigned char EXPECT[] = { 0xFE, 0xED, 0xBE, 0xEF, 0xAA, 0xAA };
	std::pair<const void*, size_t> got;
	
	ASSERT_NO_THROW({ got = pd->get_data(0); });
	
	std::vector<unsigned char> got_data   ((const unsigned char*)(got.first), (const unsigned char*)(got.first) + got.second);
	std::vector<unsigned char> expect_data(EXPECT, EXPECT + sizeof(EXPECT));
	
	EXPECT_EQ(got_data, expect_data);
	
	EXPECT_THROW({ pd->get_data(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserData, GetWString)
{
	EXPECT_THROW({ pd->get_wstring(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_wstring(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserData, GetGUID)
{
	EXPECT_THROW({ pd->get_guid(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_guid(1); }, PacketDeserialiser::Error::MissingField);
}

class PacketDeserialiserWString: public PacketDeserialiserTest {
	protected:
		virtual void SetUp() override
		{
			static const unsigned char RAW[] = {
				0x05, 0x00, 0x00, 0x00,  /* type */
				0x12, 0x00, 0x00, 0x00,  /* value_length */
				
				0x03, 0x00, 0x00, 0x00,  /* type */
				0x0A, 0x00, 0x00, 0x00,  /* value_length */
				0x48, 0x00, 0x65, 0x00,  /* value */
				0x6C, 0x00, 0x6C, 0x00,
				0x6F, 0x00,
			};
			
			ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
		}
};

TEST_F(PacketDeserialiserWString, Type)
{
	EXPECT_EQ(pd->packet_type(), (uint32_t)(5));
}

TEST_F(PacketDeserialiserWString, NumFields)
{
	EXPECT_EQ(pd->num_fields(), (size_t)(1));
}

TEST_F(PacketDeserialiserWString, IsNull)
{
	EXPECT_NO_THROW({ EXPECT_EQ(pd->is_null(0), false); });
	EXPECT_THROW({ pd->is_null(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserWString, GetDWORD)
{
	EXPECT_THROW({ pd->get_dword(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_dword(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserWString, GetData)
{
	EXPECT_THROW({ pd->get_data(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_data(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserWString, GetWString)
{
	EXPECT_NO_THROW({ EXPECT_EQ(pd->get_wstring(0), L"Hello"); });
	EXPECT_THROW({ pd->get_wstring(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserWString, GetGUID)
{
	EXPECT_THROW({ pd->get_guid(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_guid(1); }, PacketDeserialiser::Error::MissingField);
}

class PacketDeserialiserGUID: public PacketDeserialiserTest {
	protected:
		virtual void SetUp() override
		{
			static const unsigned char RAW[] = {
				0x06, 0x00, 0x00, 0x00,  /* type */
				0x18, 0x00, 0x00, 0x00,  /* value_length */
				
				0x04, 0x00, 0x00, 0x00,  /* type */
				0x10, 0x00, 0x00, 0x00,  /* value_length */
				0x01, 0x23, 0x45, 0x67,  /* value */
				0x89, 0x1A, 0xBC, 0xDE,
				0xF0, 0x12, 0x34, 0x56,
				0x78, 0x91, 0xAB, 0xCD,
			};
			
			ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
		}
};

TEST_F(PacketDeserialiserGUID, Type)
{
	EXPECT_EQ(pd->packet_type(), (uint32_t)(6));
}

TEST_F(PacketDeserialiserGUID, NumFields)
{
	EXPECT_EQ(pd->num_fields(), (size_t)(1));
}

TEST_F(PacketDeserialiserGUID, IsNull)
{
	EXPECT_NO_THROW({ EXPECT_EQ(pd->is_null(0), false); });
	EXPECT_THROW({ pd->is_null(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserGUID, GetDWORD)
{
	EXPECT_THROW({ pd->get_dword(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_dword(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserGUID, GetData)
{
	EXPECT_THROW({ pd->get_data(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_data(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserGUID, GetWString)
{
	EXPECT_THROW({ pd->get_wstring(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_wstring(1); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserGUID, GetGUID)
{
	const GUID EXPECT = { 0x67452301, 0x1A89, 0xDEBC, { 0xF0, 0x12, 0x34, 0x56, 0x78, 0x91, 0xAB, 0xCD } };
	
	EXPECT_NO_THROW({ EXPECT_EQ(pd->get_guid(0), EXPECT); });
	EXPECT_THROW({ pd->get_guid(1); }, PacketDeserialiser::Error::MissingField);
}

class PacketDeserialiserNullDWORDDataWString: public PacketDeserialiserTest {
	protected:
		virtual void SetUp() override
		{
			static const unsigned char RAW[] = {
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
			
			ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
		}
};

TEST_F(PacketDeserialiserNullDWORDDataWString, Type)
{
	EXPECT_EQ(pd->packet_type(), (uint32_t)(0x1234));
}

TEST_F(PacketDeserialiserNullDWORDDataWString, NumFields)
{
	EXPECT_EQ(pd->num_fields(), (size_t)(4));
}

TEST_F(PacketDeserialiserNullDWORDDataWString, IsNull)
{
	EXPECT_NO_THROW({ EXPECT_EQ(pd->is_null(0), true);  });
	EXPECT_NO_THROW({ EXPECT_EQ(pd->is_null(1), false); });
	EXPECT_NO_THROW({ EXPECT_EQ(pd->is_null(2), false); });
	EXPECT_NO_THROW({ EXPECT_EQ(pd->is_null(3), false); });
	
	EXPECT_THROW({ pd->is_null(4); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserNullDWORDDataWString, GetDWORD)
{
	EXPECT_THROW({ pd->get_dword(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_NO_THROW({ EXPECT_EQ(pd->get_dword(1), (DWORD)(0xEDFE)); });
	EXPECT_THROW({ pd->get_dword(2); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_dword(3); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_dword(4); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserNullDWORDDataWString, GetData)
{
	const unsigned char EXPECT[] = { 0x01, 0x23, 0x45, 0x67, 0x89 };
	std::pair<const void*, size_t> got;
	
	ASSERT_NO_THROW({ got = pd->get_data(2); });
	
	std::vector<unsigned char> got_data   ((const unsigned char*)(got.first), (const unsigned char*)(got.first) + got.second);
	std::vector<unsigned char> expect_data(EXPECT, EXPECT + sizeof(EXPECT));
	
	EXPECT_EQ(got_data, expect_data);
	
	EXPECT_THROW({ pd->get_data(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_data(1); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_data(3); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_data(4); }, PacketDeserialiser::Error::MissingField);
}

TEST_F(PacketDeserialiserNullDWORDDataWString, GetWString)
{
	EXPECT_THROW({ pd->get_wstring(0); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_wstring(1); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_THROW({ pd->get_wstring(2); }, PacketDeserialiser::Error::TypeMismatch);
	EXPECT_NO_THROW({ EXPECT_EQ(pd->get_wstring(3), L"WStr"); });
	EXPECT_THROW({ pd->get_wstring(4); }, PacketDeserialiser::Error::MissingField);
}

TEST(PacketDeserialiser, NoData)
{
	EXPECT_THROW({ PacketDeserialiser p(NULL, 0); }, PacketDeserialiser::Error::Incomplete);
}

TEST(PacketDeserialiser, PartialHeader)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
	};
	
	EXPECT_THROW({ PacketDeserialiser p(RAW, sizeof(RAW)); }, PacketDeserialiser::Error::Incomplete);
}

TEST(PacketDeserialiser, PartialData)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x04, 0x00, 0x00, 0x00,
		
		0x00, 0x00, 0x00,
	};
	
	EXPECT_THROW({ PacketDeserialiser p(RAW, sizeof(RAW)); }, PacketDeserialiser::Error::Incomplete);
}

TEST(PacketDeserialiser, ExtraData)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x08, 0x00, 0x00, 0x00,
		
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		
		0x00, 0x00
	};
	
	EXPECT_NO_THROW({ PacketDeserialiser p(RAW, sizeof(RAW)); });
}

TEST(PacketDeserialiser, FieldShortHeader)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x07, 0x00, 0x00, 0x00,
		
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
	};
	
	EXPECT_THROW({ PacketDeserialiser p(RAW, sizeof(RAW)); }, PacketDeserialiser::Error::Malformed);
}

TEST(PacketDeserialiser, FieldTooShort)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x09, 0x00, 0x00, 0x00,
		
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		
		0x00,
	};
	
	EXPECT_THROW({ PacketDeserialiser p(RAW, sizeof(RAW)); }, PacketDeserialiser::Error::Malformed);
}

TEST(PacketDeserialiser, FieldTooLong)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x09, 0x00, 0x00, 0x00,
		
		0x00, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00,
		
		0x00,
	};
	
	EXPECT_THROW({ PacketDeserialiser p(RAW, sizeof(RAW)); }, PacketDeserialiser::Error::Malformed);
}

TEST(PacketDeserialiser, ZeroLengthDWORD)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x08, 0x00, 0x00, 0x00,
		
		0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	
	PacketDeserialiser *pd = NULL;
	
	ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
	EXPECT_THROW({ pd->get_dword(0); }, PacketDeserialiser::Error::Malformed);
	
	delete pd;
}

TEST(PacketDeserialiser, UndersizeDWORD)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x0B, 0x00, 0x00, 0x00,
		
		0x01, 0x00, 0x00, 0x00,
		0x03, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
	};
	
	PacketDeserialiser *pd = NULL;
	
	ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
	EXPECT_THROW({ pd->get_dword(0); }, PacketDeserialiser::Error::Malformed);
	
	delete pd;
}

TEST(PacketDeserialiser, OversizeDWORD)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x0D, 0x00, 0x00, 0x00,
		
		0x01, 0x00, 0x00, 0x00,
		0x05, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00
	};
	
	PacketDeserialiser *pd = NULL;
	
	ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
	EXPECT_THROW({ pd->get_dword(0); }, PacketDeserialiser::Error::Malformed);
	
	delete pd;
}

TEST(PacketDeserialiser, ZeroLengthData)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x08, 0x00, 0x00, 0x00,
		
		0x02, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	
	PacketDeserialiser *pd = NULL;
	
	ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
	
	EXPECT_NO_THROW({
		auto data = pd->get_data(0);
		EXPECT_EQ(data.second, (size_t)(0));
	});
	
	delete pd;
}

TEST(PacketDeserialiser, ZeroLengthWString)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x08, 0x00, 0x00, 0x00,
		
		0x03, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	
	PacketDeserialiser *pd = NULL;
	
	ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
	EXPECT_NO_THROW({ EXPECT_EQ(pd->get_wstring(0), L""); });
	
	delete pd;
}

TEST(PacketDeserialiser, OneByteWString)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x09, 0x00, 0x00, 0x00,
		
		0x03, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00,
		0x00,
	};
	
	PacketDeserialiser *pd = NULL;
	
	ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
	EXPECT_THROW({ pd->get_wstring(0); }, PacketDeserialiser::Error::Malformed);
	
	delete pd;
}

TEST(PacketDeserialiser, ZeroLengthGUID)
{
	const unsigned char RAW[] = {
		0x01, 0x00, 0x00, 0x00,
		0x08, 0x00, 0x00, 0x00,
		
		0x04, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	
	PacketDeserialiser *pd = NULL;
	
	ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
	EXPECT_THROW({ pd->get_guid(0); }, PacketDeserialiser::Error::Malformed);
	
	delete pd;
}

TEST(PacketDeserialiser, UndersizeGUID)
{
	static const unsigned char RAW[] = {
		0x06, 0x00, 0x00, 0x00,  /* type */
		0x17, 0x00, 0x00, 0x00,  /* value_length */
		
		0x04, 0x00, 0x00, 0x00,  /* type */
		0x0F, 0x00, 0x00, 0x00,  /* value_length */
		0x01, 0x23, 0x45, 0x67,  /* value */
		0x89, 0x1A, 0xBC, 0xDE,
		0xF0, 0x12, 0x34, 0x56,
		0x78, 0x91, 0xAB,
	};
	
	PacketDeserialiser *pd = NULL;
	
	ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
	EXPECT_THROW({ pd->get_guid(0); }, PacketDeserialiser::Error::Malformed);
	
	delete pd;
}

TEST(PacketDeserialiser, OversizeGUID)
{
	static const unsigned char RAW[] = {
		0x06, 0x00, 0x00, 0x00,  /* type */
		0x19, 0x00, 0x00, 0x00,  /* value_length */
		
		0x04, 0x00, 0x00, 0x00,  /* type */
		0x11, 0x00, 0x00, 0x00,  /* value_length */
		0x01, 0x23, 0x45, 0x67,  /* value */
		0x89, 0x1A, 0xBC, 0xDE,
		0xF0, 0x12, 0x34, 0x56,
		0x78, 0x91, 0xAB, 0xCD,
		0xAA,
	};
	
	PacketDeserialiser *pd = NULL;
	
	ASSERT_NO_THROW({ pd = new PacketDeserialiser(RAW, sizeof(RAW)); });
	EXPECT_THROW({ pd->get_guid(0); }, PacketDeserialiser::Error::Malformed);
	
	delete pd;
}
