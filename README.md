# UDFS Crash Reproduction Tool - Compatibility Update

This repository contains a UDFS driver crash reproduction tool that has been rewritten for compatibility with older MinGW environments, specifically the MinGW provided in the ReactOS Build Environment (RosBE).

## Changes Made for RosBE Compatibility

The original `udfs_crash_repro.cpp` used modern C++11/14 features that are not compatible with older MinGW versions. The following changes were made:

### Replaced Modern C++ Features

1. **Threading**: Replaced `std::thread` with platform-specific threading
   - Windows: `CreateThread()` / `WaitForSingleObject()`
   - Linux: `pthread_create()` / `pthread_join()`

2. **Atomic Variables**: Replaced `std::atomic<>` with mutex-protected variables
   - Used `CRITICAL_SECTION` on Windows
   - Used `pthread_mutex_t` on Linux

3. **Lambda Functions**: Replaced with static member functions and function pointers

4. **Modern Standard Library**: 
   - Replaced `std::chrono` with platform-specific timing functions
   - Replaced `std::random_device` and `std::mt19937` with `rand()`
   - Replaced `std::uniform_int_distribution` with range calculations

5. **Language Features**:
   - Replaced `nullptr` with `NULL`
   - Replaced `auto` keyword with explicit types
   - Replaced range-based for loops with traditional for loops
   - Replaced `emplace_back()` with `push_back()`
   - Replaced uniform initialization with traditional initialization

### Compilation

The rewritten code compiles with C++98/C++03 standards and is compatible with:

- **RosBE MinGW (GCC 8.4.0)**: `g++ -o udfs_crash_repro.exe udfs_crash_repro.cpp -std=c++98 -static-libgcc -static-libstdc++`
- **Modern Cross-compiler**: `i686-w64-mingw32-g++ -o udfs_crash_repro.exe udfs_crash_repro.cpp -std=c++98`
- **Linux Demo**: `g++ -o udfs_crash_repro udfs_crash_repro.cpp -std=c++98 -DLINUX_DEMO -lpthread`

### Testing

The tool has been tested to compile successfully with:
- C++98 standard
- C++03 standard 
- Modern MinGW cross-compiler
- Linux pthread implementation

### Functionality

All original functionality has been preserved:
- Multi-threaded file stress testing
- Overflow queue race condition targeting
- Memory pressure operations
- Strategic timing controls
- Idle priority threading for system responsiveness

The tool maintains the same command-line interface and behavior as the original version.