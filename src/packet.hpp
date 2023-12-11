/* DirectPlay Lite
 * Copyright (C) 2018-2023 Daniel Collins <solemnwarning@solemnwarning.net>
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

#ifndef DPLITE_PACKET_HPP
#define DPLITE_PACKET_HPP

#include <stdexcept>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

struct TLVChunk
{
	uint32_t type;
	uint32_t value_length;

#pragma warning(suppress: 4200)
	unsigned char value[0];
};

class PacketSerialiser
{
	private:
		std::vector<unsigned char> sbuf;
		
	public:
		PacketSerialiser(uint32_t type);
		
		std::pair<const void*, size_t> raw_packet() const;
		
		void append_null();
		void append_dword(DWORD value);
		void append_data(const void *data, size_t size);
		void append_wstring(const std::wstring &string);
		void append_guid(const GUID &guid);
};

class PacketDeserialiser
{
	private:
		const TLVChunk *header;
		std::vector<const TLVChunk*> fields;
		
	public:
		class Error: public std::runtime_error
		{
			protected:
				Error(const std::string &what): runtime_error(what) {}
				
			public:
				class Incomplete;
				class Malformed;
				class MissingField;
				class TypeMismatch;
		};
		
		PacketDeserialiser(const void *serialised_packet, size_t packet_size);
		
		uint32_t packet_type() const;
		size_t num_fields() const;
		
		bool is_null(size_t index) const;
		DWORD get_dword(size_t index) const;
		std::pair<const void*,size_t> get_data(size_t index) const;
		std::wstring get_wstring(size_t index) const;
		GUID get_guid(size_t index) const;
};

class PacketDeserialiser::Error::Incomplete: public Error
{
	public:
		Incomplete(const std::string &what = "Incomplete packet"): Error(what) {}
};

class PacketDeserialiser::Error::Malformed: public Error
{
	public:
		Malformed(const std::string &what = "Malformed packet"): Error(what) {}
};

class PacketDeserialiser::Error::MissingField: public Error
{
	public:
		MissingField(const std::string &what = "Missing field in packet"): Error(what) {}
};

class PacketDeserialiser::Error::TypeMismatch: public Error
{
	public:
		TypeMismatch(const std::string &what = "Incorrect field type in packet"): Error(what) {}
};

#endif /* !DPLITE_PACKET_HPP */
