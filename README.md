# LocalSend

Send files or folders over local network using TCP protocol, supports Windows and Linux operating systems.

## Support

- Send multiple paths at once
- Send file or folder paths
- Send relative and absolute paths
- Wildcard patterns (e.g., \*.txt)
- Path containing spaces
- Path containing Chinese characters
- Files larger than 4GB
- Windows and Linux operating system (not tested on macOS)
- Older versions of Windows, such as Win XP and Win7
- 32-bit and 64-bit operating system
- Sending progress bar
- Sending success summary
- Automatic renaming of duplicate files

## Build

Using `std::filesystem` can significantly simplify iterating files in directories, so it needs to be compiled with a `C++17` supporting compiler:

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

The files will be received in the path where the server started, with no subdir.

Duplicate files will auto added serial number before file extension, such as "name_2.txt".
