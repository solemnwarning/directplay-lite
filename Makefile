CXX := i686-w64-mingw32-g++
CXXFLAGS := -std=c++11 -Wall

all: dpnet.dll

dpnet.dll: src/dpnet.o src/dpnet.def src/DirectPlay8Address.o src/DirectPlay8Peer.o
	$(CXX) $(CXXFLAGS) -Wl,--enable-stdcall-fixup -shared -o $@ $^ -ldxguid -static-libstdc++ -static-libgcc

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
