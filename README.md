# LocalSend

Send files or folders over local network using TCP protocol, supports Windows and Linux operating systems.

## Build

Using `std::filesystem` can significantly simplify iterating files in directories, so it needs to be compiled with a C++17 supporting compiler:

```bash
g++ -std=c++17 localsend.cpp -lws2_32 -o localsend.exe
```

## Usage

1. **Start server (receive files):**

```bash
localsend.exe
```

2. **Start client (send files):**

```bash
localsend.exe IP PATH1 PATH2 ...
```
