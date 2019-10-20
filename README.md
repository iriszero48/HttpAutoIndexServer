# HttpAutoIndexServer
Http Auto Index Server(Windows/Linux)
## Usage
    ./HttpAutoIndexServer.out IndexPath Port threadNum Coding IcoPath
## Compile
### CMake
    cmake HttpAutoIndexServer && make
### GCC
    g++ HttpAutoIndexServer/main.cpp -o HttpAutoIndexServer.out -std=c++17 -pthread
### Clang
    clang++ HttpAutoIndexServer/main.cpp -o HttpAutoIndexServer.out -std=c++17 -pthread
## Release
### HttpAutoIndexServer.DEBUG.win10.x64.exe
    Windows SDK 10.0.16299.0
    Visual Studio 2017 (v141)
    使用多字节字符集
### HttpAutoIndexServer.DEBUG.ubuntu1804.x64.out
    g++ (Ubuntu 7.4.0-1ubuntu1~18.04) 7.4.0
    Linux TreeDiagram 4.4.0-18362-Microsoft #1-Microsoft Mon Mar 18 12:02:00 PST 2019 x86_64 x86_64 x86_64 GNU/Linux
    g++ -o HttpAutoIndexServer.out -O3 -Wl,--no-undefined -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack HttpAutoIndexServer/main.cpp -pthread -std=c++17
