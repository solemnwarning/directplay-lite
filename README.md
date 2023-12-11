# DirectPlay Lite

## What is this?

DirectPlay Lite is an open-source reimplementation of Microsoft's DirectPlay API, intended to help preseve old games as official support for DirectPlay wanes.

Rather than being a full reimplementation of the DirectPlay specification, DirectPlay Lite aims to provide a compatible API to games, while removing some of the other complexity, such as support for multiple service providers - games requesting the TCP/IP or IPX service providers will actually use IP, masquerading as another protocol where necessary.

**NOTE**: This is currently early in development and aimed at people or companies repackaging old games. Only a limited portion of the DirectPlay (8) APIs are implemented and installing it isn't user-friendly.

## Building

Use the `directplay-lite.sln` solution in Visual Studio 2017 or later.

## Using

DirectPlay Lite can be loaded into a game using the two following methods.

### Installation as a COM DLL

This is the way DirectPlay and COM were designed to operate. The DirectPlay class registrations are added to the registry and the DLLs are copied somewhere for all applications to use.

### Registration from a hook DLL

This method is less invasive, but depends on undefined behaviour and may not work everywhere. A "hook" DLL is placed alongside the game, which masks one of the libraries it usually loads. When the hook DLL is loaded, it hooks some of the COM APIs in order to inject the DirectPlay class registrations and loads the masked library, redirecting any function calls into it.

The following hook DLLs are currently built:

 * ddraw.dll
 * dsound.dll

New hook DLLs can be built by dumping a list of exported functions from the DLL you want to wrap, producing a stub assembly listing with `mkstubs.pl` and updating `build.bat` to build it.

**NOTE**: Only ONE hook DLL should be used.

## Copyright

Copyright © 2018 Daniel Collins <solemnwarning@solemnwarning.net>

Includes the [Minhook](https://github.com/TsudaKageyu/minhook) library by Tsuda Kageyu.
