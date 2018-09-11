CXX := i686-w64-mingw32-g++
CXXFLAGS := -std=c++11 -Wall -D_WIN32_WINNT=0x0600 -ggdb

TEST_CXXFLAGS := $(CXXFLAGS) -I./googletest/include/

all: dpnet.dll

check: tests/all-tests.exe
ifeq ($(OS),Windows_NT)
	tests/all-tests.exe
else
	wine tests/all-tests.exe
endif

dpnet.dll: src/dpnet.o src/dpnet.def src/DirectPlay8Address.o src/DirectPlay8Peer.o src/network.o src/packet.o src/SendQueue.o src/AsyncHandleAllocator.o src/HostEnumerator.o src/COMAPIException.o
	$(CXX) $(CXXFLAGS) -Wl,--enable-stdcall-fixup -shared -o $@ $^ -ldxguid -lws2_32 -static-libstdc++ -static-libgcc

tests/DirectPlay8Address.exe: tests/DirectPlay8Address.o src/DirectPlay8Address.o googletest/src/gtest-all.o googletest/src/gtest_main.o
	$(CXX) $(TEST_CXXFLAGS) -o $@ $^  -ldxguid -lole32 -static-libstdc++ -static-libgcc

tests/PacketSerialiser.exe: tests/PacketSerialiser.o src/packet.o googletest/src/gtest-all.o googletest/src/gtest_main.o
	$(CXX) $(TEST_CXXFLAGS) -o $@ $^  -ldxguid -lole32 -static-libstdc++ -static-libgcc

tests/all-tests.exe: tests/DirectPlay8Address.o src/DirectPlay8Address.o \
	tests/PacketSerialiser.o tests/PacketDeserialiser.o src/packet.o \
	tests/DirectPlay8Peer.o src/DirectPlay8Peer.o \
	src/SendQueue.o src/AsyncHandleAllocator.o src/HostEnumerator.o src/network.o src/COMAPIException.o \
	googletest/src/gtest-all.o googletest/src/gtest_main.o
	$(CXX) $(TEST_CXXFLAGS) -o $@ $^  -ldxguid -lole32 -static-libstdc++ -static-libgcc -lws2_32

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

tests/%.o: tests/%.cpp
	$(CXX) $(TEST_CXXFLAGS) -c -o $@ $<

googletest/src/%.o: googletest/src/%.cc
	$(CXX) $(TEST_CXXFLAGS) -I./googletest/ -c -o $@ $<
