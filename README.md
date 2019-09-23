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
