/*
 * UDFS Driver REAL KERNEL CRASH Test Program
 * 
 * WARNING: THIS PROGRAM IS DESIGNED TO CRASH THE KERNEL!
 * Only run this if you want to trigger the actual UDFS driver BSOD for debugging purposes.
 * 
 * This program triggers the real race condition in the ReactOS UDFS driver that causes:
 * UNEXPECTED_KERNEL_MODE_TRAP (0x7F) - when RemoveHeadList() is called on corrupted list
 * 
 * Requirements:
 * - Must be run on ReactOS with the UNFIXED UDFS driver
 * - Must be run on a UDF filesystem (UDF 2.01 recommended)
 * - Sufficient I/O load to trigger the race condition
 * 
 * Compile with: 
 *   Windows/ReactOS: cl udfs_crash_repro.cpp /EHsc /std:c++11
 *   Cross-compile: x86_64-w64-mingw32-g++ -std=c++11 -o udfs_crash_repro.exe udfs_crash_repro.cpp -pthread
 *   Linux (demo): g++ -std=c++11 -o udfs_crash_repro udfs_crash_repro.cpp -pthread -DLINUX_DEMO
 * 
 * Run with: ./udfs_crash_repro [path_to_udf_drive]
 */

// Ensure C++11 or later is being used
#if __cplusplus < 201103L
#error "This code requires C++11 or later. Please compile with -std=c++11 or higher."
#endif

#ifdef LINUX_DEMO
// Linux demo version - shows what the program would do on Windows
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <string>
#include <atomic>
#include <memory>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>

// Mock Windows types for Linux demo
typedef unsigned long DWORD;
typedef void* HANDLE;
typedef struct { 
    char cFileName[260]; 
} WIN32_FIND_DATAA;
const HANDLE INVALID_HANDLE_VALUE = (HANDLE)-1;
const DWORD GENERIC_READ = 0x80000000;
const DWORD GENERIC_WRITE = 0x40000000;
const DWORD CREATE_ALWAYS = 2;
const DWORD FILE_ATTRIBUTE_NORMAL = 0x80;
const DWORD FILE_FLAG_WRITE_THROUGH = 0x80000000;
const DWORD FILE_FLAG_NO_BUFFERING = 0x20000000;
const DWORD FILE_SHARE_READ = 1;
const DWORD FILE_SHARE_WRITE = 2;
const DWORD FILE_BEGIN = 0;
const DWORD FILE_END = 2;

// Mock Windows functions for Linux demo
HANDLE CreateFileA(const char* filename, DWORD access, DWORD sharing, void* security, DWORD creation, DWORD flags, HANDLE template_file) {
    std::cout << "[MOCK] CreateFile: " << filename << " (flags: 0x" << std::hex << flags << std::dec << ")" << std::endl;
    return (HANDLE)1; // Fake success
}

bool WriteFile(HANDLE file, const void* buffer, DWORD size, DWORD* written, void* overlapped) {
    *written = size;
    std::cout << "[MOCK] WriteFile: " << size << " bytes" << std::endl;
    return true;
}

bool ReadFile(HANDLE file, void* buffer, DWORD size, DWORD* read, void* overlapped) {
    *read = size;
    std::cout << "[MOCK] ReadFile: " << size << " bytes" << std::endl;
    return true;
}

bool FlushFileBuffers(HANDLE file) {
    std::cout << "[MOCK] FlushFileBuffers" << std::endl;
    return true;
}

bool CloseHandle(HANDLE handle) {
    std::cout << "[MOCK] CloseHandle" << std::endl;
    return true;
}

bool DeleteFileA(const char* filename) {
    std::cout << "[MOCK] DeleteFile: " << filename << std::endl;
    return true;
}

DWORD SetFilePointer(HANDLE file, long distance, long* high, DWORD method) {
    std::cout << "[MOCK] SetFilePointer" << std::endl;
    return 0;
}

bool GetVolumeInformationA(const char* root, char* volume, DWORD vol_size, DWORD* serial, DWORD* max_len, DWORD* flags, char* fs_name, DWORD fs_size) {
    strcpy(fs_name, "ext4");  // Simulate non-UDF filesystem on Linux
    strcpy(volume, "Linux Demo");
    return true;
}

bool CreateDirectoryA(const char* path, void* security) {
    mkdir(path, 0755);
    return true;
}

bool RemoveDirectoryA(const char* path) {
    std::cout << "[MOCK] RemoveDirectory: " << path << std::endl;
    rmdir(path);
    return true;
}

HANDLE FindFirstFileA(const char* pattern, WIN32_FIND_DATAA* data) {
    std::cout << "[MOCK] FindFirstFile: " << pattern << std::endl;
    return INVALID_HANDLE_VALUE; // No files found
}

bool FindNextFileA(HANDLE find, WIN32_FIND_DATAA* data) { return false; }
bool FindClose(HANDLE find) { return true; }

#else
// Real Windows implementation
// Ensure proper threading support for MinGW
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <string>
#include <atomic>
#include <memory>
#include <windows.h>
#endif

class UDFSCrashTrigger {
private:
    std::string testPath;
    std::atomic<bool> shouldStop{false};
    std::atomic<int> activeThreads{0};
    
    // Strategic test parameters designed to target specific race condition timing
    static const int NUM_WORKER_THREADS = 12;     // Moderate thread count for optimal race conditions
    static const int FILES_PER_THREAD = 30;       // Focused file burst per iteration
    static const int MAX_FILE_SIZE = 64 * 1024;   // Moderate file sizes (64KB) for realistic I/O
    static const int MIN_FILE_SIZE = 512;         // Reasonable minimum (512B) for proper I/O timing
    static const int ITERATION_COUNT = 1000;      // Sufficient iterations without overwhelming
    static const int STRATEGIC_BURST_SIZE = 25;   // Strategic burst operations with timing gaps
    static const int OVERFLOW_STRESS_CYCLES = 200; // Focused overflow queue stress cycles
    static const int RACE_CONDITION_THREADS = 4;  // Optimal threads for race condition timing
    
public:
    UDFSCrashTrigger(const std::string& path) : testPath(path) {
        if (testPath.back() != '\\' && testPath.back() != '/') {
            testPath += "\\";
        }
    }
    
    // Check if we're running on a UDF filesystem
    bool IsUDFFilesystem() {
        char volumeName[260]; // MAX_PATH equivalent
        char fsName[260];
        DWORD serialNumber, maxComponentLength, fsFlags;
        
#ifdef LINUX_DEMO
        std::cout << "\n=== LINUX DEMO MODE ===" << std::endl;
        std::cout << "This is a demonstration of what the program would do on Windows/ReactOS." << std::endl;
        std::cout << "On a real UDF filesystem with unfixed driver, this would trigger kernel crash." << std::endl;
        std::cout << "========================" << std::endl;
#endif
        
        std::string rootPath = testPath.substr(0, 3); // Extract "C:\" style root
        
        if (GetVolumeInformationA(
            rootPath.c_str(),
            volumeName, sizeof(volumeName),
            &serialNumber,
            &maxComponentLength,
            &fsFlags,
            fsName, sizeof(fsName))) {
            
            std::cout << "Filesystem: " << fsName << std::endl;
            std::cout << "Volume: " << volumeName << std::endl;
            
#ifdef LINUX_DEMO
            std::cout << "\n[LINUX DEMO] On ReactOS with UDF filesystem, this would show 'UDF' instead of '" << fsName << "'" << std::endl;
            return false; // Simulate non-UDF for demo
#else
            // Check if it's UDF
            if (strstr(fsName, "UDF") != nullptr) {
                std::cout << "✓ UDF filesystem detected!" << std::endl;
                return true;
            } else {
                std::cout << "⚠ Warning: Not a UDF filesystem. This test may not trigger the bug." << std::endl;
                std::cout << "  For best results, run on a UDF 2.01 formatted drive." << std::endl;
                return false;
            }
#endif
        }
        
        std::cout << "⚠ Warning: Could not determine filesystem type." << std::endl;
        return false;
    }
    
    // Strategic overflow queue stress targeting specific race condition timing
    void OverflowQueueStressWorker(int threadId) {
        activeThreads++;
        
        // Set thread to idle priority to reduce system impact
#ifndef LINUX_DEMO
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
#else
        std::cout << "[MOCK] SetThreadPriority(THREAD_PRIORITY_IDLE)" << std::endl;
#endif
        
        std::cout << "OverflowQueue stress thread " << threadId << " started (idle priority)" << std::endl;
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> sizeDis(MIN_FILE_SIZE, MAX_FILE_SIZE);
        
        try {
            for (int cycle = 0; cycle < OVERFLOW_STRESS_CYCLES && !shouldStop; ++cycle) {
                // Strategic Pattern: Timed create/delete to trigger queue state inconsistency
                std::vector<std::thread> raceTriggers;
                for (int i = 0; i < RACE_CONDITION_THREADS; ++i) {
                    raceTriggers.emplace_back([this, threadId, cycle, i, &sizeDis, &gen]() {
                        for (int rapid = 0; rapid < 15 && !shouldStop; ++rapid) { // Reduced from 50 to 15
                            std::string filename = testPath + "overflow_t" + std::to_string(threadId) + 
                                                  "_c" + std::to_string(cycle) + "_r" + std::to_string(i) + 
                                                  "_x" + std::to_string(rapid) + ".tmp";
                            
                            // Strategic create with moderate flags for better timing
                            HANDLE hFile = CreateFileA(
                                filename.c_str(),
                                GENERIC_WRITE | GENERIC_READ,
                                FILE_SHARE_READ,  // Allow some sharing to reduce extreme contention
                                nullptr,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,  // Removed NO_BUFFERING for better timing
                                nullptr
                            );
                            
                            if (hFile != INVALID_HANDLE_VALUE) {
                                size_t fileSize = sizeDis(gen);
                                auto data = GenerateRandomData(fileSize);
                                
                                DWORD bytesWritten;
                                WriteFile(hFile, data.data(), fileSize, &bytesWritten, nullptr);
                                
                                // Strategic seek operations with timing gaps
                                SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
                                std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Small delay for race timing
                                SetFilePointer(hFile, fileSize / 2, nullptr, FILE_BEGIN);
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                SetFilePointer(hFile, 0, nullptr, FILE_END);
                                
                                FlushFileBuffers(hFile);
                                CloseHandle(hFile);
                            }
                            
                            // Strategic delay to allow race condition timing window
                            std::this_thread::sleep_for(std::chrono::microseconds(500));
                            DeleteFileA(filename.c_str());
                        }
                    });
                }
                
                // Wait for all race triggers to complete
                for (auto& racer : raceTriggers) {
                    racer.join();
                }
                
                // Strategic delay between cycles to prevent CPU saturation
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                
                if (cycle % 50 == 0) {
                    std::cout << "OverflowQueue thread " << threadId << " completed " << cycle << " cycles" << std::endl;
                }
            }
        } catch (...) {
            std::cout << "OverflowQueue thread " << threadId << " caught exception" << std::endl;
        }
        
        std::cout << "OverflowQueue thread " << threadId << " finished" << std::endl;
        activeThreads--;
    }
    
    // Strategic memory pressure operations to amplify race conditions with controlled timing
    void MemoryPressureWorker(int threadId) {
        activeThreads++;
        
        // Set thread to idle priority to reduce system impact
#ifndef LINUX_DEMO
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
#else
        std::cout << "[MOCK] SetThreadPriority(THREAD_PRIORITY_IDLE)" << std::endl;
#endif
        
        std::cout << "Memory pressure thread " << threadId << " started (idle priority)" << std::endl;
        
        try {
            for (int round = 0; round < 50 && !shouldStop; ++round) { // Reduced from 100 to 50
                // Create moderate number of files with strategic timing
                std::vector<HANDLE> handles;
                std::vector<std::string> filenames;
                
                for (int i = 0; i < 15 && !shouldStop; ++i) { // Reduced from 50 to 15
                    std::string filename = testPath + "pressure_t" + std::to_string(threadId) + 
                                          "_r" + std::to_string(round) + "_f" + std::to_string(i) + ".tmp";
                    
                    HANDLE hFile = CreateFileA(
                        filename.c_str(),
                        GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                        nullptr
                    );
                    
                    if (hFile != INVALID_HANDLE_VALUE) {
                        handles.push_back(hFile);
                        filenames.push_back(filename);
                        
                        // Write reasonable amount of data
                        auto data = GenerateRandomData(MIN_FILE_SIZE * 2);
                        DWORD written;
                        WriteFile(hFile, data.data(), data.size(), &written, nullptr);
                        
                        // Strategic delay to allow I/O completion
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                }
                
                // Strategic delay before cleanup to allow race condition timing
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                
                // Now close handles with strategic timing
                for (HANDLE handle : handles) {
                    FlushFileBuffers(handle);
                    CloseHandle(handle);
                    std::this_thread::sleep_for(std::chrono::microseconds(100)); // Micro-delay for timing
                }
                
                // Strategic delay before deletion
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                
                // Delete files with strategic timing
                for (const std::string& filename : filenames) {
                    DeleteFileA(filename.c_str());
                    std::this_thread::sleep_for(std::chrono::microseconds(200)); // Micro-delay for timing
                }
                
                if (round % 10 == 0) {
                    std::cout << "Memory pressure thread " << threadId << " completed " << round << " rounds" << std::endl;
                }
                
                // Strategic delay between rounds to prevent CPU saturation
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        } catch (...) {
            std::cout << "Memory pressure thread " << threadId << " caught exception" << std::endl;
        }
        
        std::cout << "Memory pressure thread " << threadId << " finished" << std::endl;
        activeThreads--;
    }
    std::vector<char> GenerateRandomData(size_t size) {
        std::vector<char> data(size);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<char>(dis(gen));
        }
        return data;
    }
    
    // Create and delete files rapidly to stress the driver
    void FileStressWorker(int threadId) {
        activeThreads++;
        
        // Set thread to idle priority to reduce system impact
#ifndef LINUX_DEMO
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
#else
        std::cout << "[MOCK] SetThreadPriority(THREAD_PRIORITY_IDLE)" << std::endl;
#endif
        
        std::cout << "Thread " << threadId << " started (idle priority)" << std::endl;
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> sizeDis(MIN_FILE_SIZE, MAX_FILE_SIZE);
        std::uniform_int_distribution<> burstDis(5, STRATEGIC_BURST_SIZE);
        
        try {
            for (int iteration = 0; iteration < ITERATION_COUNT && !shouldStop; ++iteration) {
                std::vector<std::string> createdFiles;
                
                // Phase 1: Strategic burst file creation with controlled timing
                int burstSize = burstDis(gen);
                for (int i = 0; i < burstSize; ++i) {
                    if (shouldStop) break;
                    
                    std::string filename = testPath + "stress_t" + std::to_string(threadId) + 
                                          "_i" + std::to_string(iteration) + "_f" + std::to_string(i) + ".tmp";
                    
                    size_t fileSize = sizeDis(gen);
                    auto data = GenerateRandomData(fileSize);
                    
                    // Create file with strategic flags for race condition timing
                    HANDLE hFile = CreateFileA(
                        filename.c_str(),
                        GENERIC_WRITE | GENERIC_READ,
                        FILE_SHARE_READ,  // Allow sharing to reduce extreme contention
                        nullptr,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, // Removed NO_BUFFERING for better timing
                        nullptr
                    );
                    
                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD bytesWritten;
                        WriteFile(hFile, data.data(), fileSize, &bytesWritten, nullptr);
                        FlushFileBuffers(hFile); // Force immediate flush
                        
                        // Strategic read-back with timing control
                        SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
                        std::vector<char> readBuffer(fileSize);
                        DWORD bytesRead;
                        ReadFile(hFile, readBuffer.data(), fileSize, &bytesRead, nullptr);
                        
                        CloseHandle(hFile);
                        createdFiles.push_back(filename);
                    }
                    
                    // Strategic micro-delay to control I/O timing for race conditions
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
                
                // Phase 2: Strategic concurrent file operations to create controlled race conditions
                std::vector<std::thread> concurrentOps;
                for (int i = 0; i < 6 && !shouldStop; ++i) { // Reduced from 16 to 6 concurrent operations
                    concurrentOps.emplace_back([this, threadId, iteration, i]() {
                        std::string concurrentFile = testPath + "race_t" + std::to_string(threadId) + 
                                                   "_i" + std::to_string(iteration) + "_r" + std::to_string(i) + ".tmp";
                        
                        // Strategic rapid create/delete cycles with controlled timing
                        for (int cycle = 0; cycle < 25 && !shouldStop; ++cycle) { // Reduced from 100 to 25 cycles
                            HANDLE hFile = CreateFileA(
                                concurrentFile.c_str(),
                                GENERIC_WRITE | GENERIC_READ,
                                FILE_SHARE_READ, // Reduced contention
                                nullptr,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, // Removed DELETE_ON_CLOSE and NO_BUFFERING
                                nullptr
                            );
                            
                            if (hFile != INVALID_HANDLE_VALUE) {
                                auto data = GenerateRandomData(1024); // Fixed size for consistency
                                DWORD written;
                                WriteFile(hFile, data.data(), data.size(), &written, nullptr);
                                
                                // Strategic file manipulation with timing control
                                SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
                                std::this_thread::sleep_for(std::chrono::microseconds(100));
                                SetFilePointer(hFile, 512, nullptr, FILE_BEGIN);
                                std::this_thread::sleep_for(std::chrono::microseconds(100));
                                SetFilePointer(hFile, 0, nullptr, FILE_END);
                                
                                FlushFileBuffers(hFile);
                                
                                // Read operations with strategic timing
                                SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
                                std::vector<char> readBuf(1024);
                                DWORD read;
                                ReadFile(hFile, readBuf.data(), readBuf.size(), &read, nullptr);
                                
                                CloseHandle(hFile);
                            }
                            
                            // Strategic delay before deletion to create race timing window
                            std::this_thread::sleep_for(std::chrono::microseconds(300));
                            DeleteFileA(concurrentFile.c_str());
                            
                            // Strategic delay between cycles to allow proper timing
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    });
                }
                
                // Phase 3: Directory operations with strategic timing
                std::string testDir = testPath + "dir_t" + std::to_string(threadId) + "_i" + std::to_string(iteration);
                CreateDirectoryA(testDir.c_str(), nullptr);
                
                // Create moderate number of files in directory with strategic timing
                for (int i = 0; i < 5 && !shouldStop; ++i) { // Reduced from 10 to 5
                    std::string dirFile = testDir + "\\file" + std::to_string(i) + ".tmp";
                    HANDLE hFile = CreateFileA(
                        dirFile.c_str(),
                        GENERIC_WRITE,
                        FILE_SHARE_READ,
                        nullptr,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                        nullptr
                    );
                    
                    if (hFile != INVALID_HANDLE_VALUE) {
                        auto data = GenerateRandomData(2048);
                        DWORD written;
                        WriteFile(hFile, data.data(), data.size(), &written, nullptr);
                        CloseHandle(hFile);
                    }
                    
                    // Strategic delay between file creations
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                
                // Wait for concurrent operations to complete
                for (auto& op : concurrentOps) {
                    op.join();
                }
                
                // Phase 4: Strategic deletion burst to trigger the race condition
                for (const auto& filename : createdFiles) {
                    if (shouldStop) break;
                    DeleteFileA(filename.c_str());
                    std::this_thread::sleep_for(std::chrono::microseconds(100)); // Strategic micro-delay
                }
                
                // Clean up directory files with strategic timing
                for (int i = 0; i < 5; ++i) {
                    std::string dirFile = testDir + "\\file" + std::to_string(i) + ".tmp";
                    DeleteFileA(dirFile.c_str());
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                RemoveDirectoryA(testDir.c_str());
                
                // Phase 5: Strategic file attribute operations with controlled timing
                std::string attrTestFile = testPath + "attr_t" + std::to_string(threadId) + ".tmp";
                for (int attr = 0; attr < 8 && !shouldStop; ++attr) { // Reduced from 20 to 8
                    HANDLE hFile = CreateFileA(
                        attrTestFile.c_str(),
                        GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        nullptr
                    );
                    
                    if (hFile != INVALID_HANDLE_VALUE) {
                        auto data = GenerateRandomData(1024);
                        DWORD written;
                        WriteFile(hFile, data.data(), data.size(), &written, nullptr);
                        FlushFileBuffers(hFile);
                        
                        // Strategic file pointer operations with timing
                        SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                        SetFilePointer(hFile, 512, nullptr, FILE_BEGIN);
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                        SetFilePointer(hFile, 0, nullptr, FILE_END);
                        
                        CloseHandle(hFile);
                    }
                    DeleteFileA(attrTestFile.c_str());
                    std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Strategic delay
                }
                
                if (iteration % 25 == 0) {
                    std::cout << "Thread " << threadId << " completed " << iteration << " iterations (burst size: " << burstSize << ")" << std::endl;
                }
                
                // Strategic delay between iterations to prevent CPU saturation
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        } catch (...) {
            std::cout << "Thread " << threadId << " caught exception" << std::endl;
        }
        
        std::cout << "Thread " << threadId << " finished" << std::endl;
        activeThreads--;
    }
    
    // Main stress test that tries to trigger the UDFS race condition
    void RunKernelCrashTest() {
        std::cout << "\n===========================================" << std::endl;
        std::cout << "UDFS KERNEL CRASH TEST" << std::endl;
        std::cout << "===========================================" << std::endl;
        std::cout << "⚠️  WARNING: THIS TEST IS DESIGNED TO CRASH THE KERNEL!" << std::endl;
        std::cout << "⚠️  SAVE ALL YOUR WORK BEFORE RUNNING!" << std::endl;
        std::cout << "===========================================" << std::endl;
        
        std::cout << "Test path: " << testPath << std::endl;
        IsUDFFilesystem();
        
        std::cout << "\nStarting strategic stress test with:" << std::endl;
        std::cout << "- " << NUM_WORKER_THREADS << " file worker threads (optimized for race conditions, idle priority)" << std::endl;
        std::cout << "- " << FILES_PER_THREAD << " files per thread per iteration (strategic load)" << std::endl;
        std::cout << "- " << ITERATION_COUNT << " iterations per thread (sufficient coverage)" << std::endl;
        std::cout << "- File sizes: " << MIN_FILE_SIZE << " - " << MAX_FILE_SIZE << " bytes (realistic I/O timing)" << std::endl;
        std::cout << "- " << STRATEGIC_BURST_SIZE << " max burst operations per iteration (controlled)" << std::endl;
        std::cout << "- " << OVERFLOW_STRESS_CYCLES << " overflow queue stress cycles (targeted)" << std::endl;
        std::cout << "- " << RACE_CONDITION_THREADS << " race condition threads per worker (optimal timing)" << std::endl;
        
        std::cout << "\nStrategic attack patterns to trigger race condition efficiently:" << std::endl;
        std::cout << "1. IDLE PRIORITY THREADS: All worker threads run at idle priority for better system responsiveness" << std::endl;
        std::cout << "2. CONTROLLED TIMING: Micro-delays between operations for optimal race windows" << std::endl;
        std::cout << "3. STRATEGIC CONCURRENCY: " << RACE_CONDITION_THREADS << " threads per worker with controlled timing" << std::endl;
        std::cout << "4. OVERFLOW QUEUE FOCUS: Dedicated workers specifically targeting overflow queue race" << std::endl;
        std::cout << "5. MEMORY PRESSURE: Controlled memory operations with strategic timing" << std::endl;
        std::cout << "6. TIMING WINDOWS: Strategic delays to create proper race condition timing" << std::endl;
        std::cout << "7. LOW SYSTEM IMPACT: Idle priority reduces CPU usage and improves responsiveness" << std::endl;
        std::cout << "8. REALISTIC I/O: Proper file sizes and buffering for realistic driver stress" << std::endl;
        std::cout << "9. TARGETED PATTERNS: Focus on specific I/O patterns that stress overflow queue" << std::endl;
        
        std::cout << "\nPress Enter to start the strategic test designed to trigger the race condition..." << std::endl;
#ifdef LINUX_DEMO
        std::cout << "[LINUX DEMO] In real ReactOS, this would attempt to trigger the kernel crash!" << std::endl;
#endif
        std::cin.get();
        
        std::cout << "\nStarting strategic kernel crash test..." << std::endl;
        
        // Start ALL types of worker threads for maximum stress
        std::vector<std::thread> workers;
        
        // 1. Main file stress workers (most threads)
        for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
            workers.emplace_back(&UDFSCrashTrigger::FileStressWorker, this, i);
        }
        
        // 2. Overflow queue specific stress workers  
        for (int i = 0; i < NUM_WORKER_THREADS / 4; ++i) { // 1/4 as many overflow workers
            workers.emplace_back(&UDFSCrashTrigger::OverflowQueueStressWorker, this, i + 1000);
        }
        
        // 3. Memory pressure workers
        for (int i = 0; i < NUM_WORKER_THREADS / 8; ++i) { // 1/8 as many memory workers
            workers.emplace_back(&UDFSCrashTrigger::MemoryPressureWorker, this, i + 2000);
        }
        
        std::cout << "Total threads launched: " << workers.size() << " (" << NUM_WORKER_THREADS << " file + " 
                  << (NUM_WORKER_THREADS / 4) << " overflow + " << (NUM_WORKER_THREADS / 8) << " memory)" << std::endl;
        
        // Monitor progress and show status with better timing
        auto startTime = std::chrono::steady_clock::now();
        while (activeThreads > 0) {
            std::this_thread::sleep_for(std::chrono::seconds(15)); // Check every 15 seconds (less frequent)
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count();
            std::cout << "Active threads: " << activeThreads << ", Elapsed: " << elapsed << "s" << std::endl;
            std::cout << "    STATUS: Strategic stress with " << workers.size() << " total threads targeting UDFS race condition..." << std::endl;
            
            // Show progress updates every 30 seconds
            if (elapsed % 30 == 0 && elapsed > 0) {
                std::cout << "    PROGRESS: " << elapsed << "s elapsed - Strategic I/O patterns targeting overflow queue" << std::endl;
            }
            
            // Extended timeout to 10 minutes since we're using strategic timing
            if (elapsed > 600) {
                std::cout << "\nStrategic stress running for 10+ minutes without crash. This suggests:" << std::endl;
                std::cout << "1. Race condition may require very specific timing/hardware conditions" << std::endl;
                std::cout << "2. Driver may be fixed or environment doesn't reproduce the issue" << std::endl;
                std::cout << "3. UDFS overflow queue may be handling strategic stress without race condition" << std::endl;
                std::cout << "4. Consider running on different hardware/VM configuration" << std::endl;
                std::cout << "5. Try running multiple instances simultaneously for increased contention" << std::endl;
                shouldStop = true;
            }
        }
        
        // Wait for all threads to complete
        for (auto& worker : workers) {
            worker.join();
        }
        
        std::cout << "\nStrategic stress test completed without kernel crash." << std::endl;
        std::cout << "This strategic version used optimized techniques for race condition targeting:" << std::endl;
        std::cout << "- 12 file threads, 3 overflow threads, 1 memory pressure thread (all idle priority)" << std::endl;
        std::cout << "- 30 files per burst, 1000 iterations, 25 burst size" << std::endl;
        std::cout << "- 512B-64KB files (realistic I/O timing), strategic micro-delays" << std::endl;
        std::cout << "- 6 concurrent operations per worker, 25 cycles each" << std::endl;
        std::cout << "- Strategic timing delays, controlled file sharing" << std::endl;
        std::cout << "- Idle priority threads for minimal system impact and better responsiveness" << std::endl;
        std::cout << "- Focused overflow queue race condition targeting with proper timing" << std::endl;
        std::cout << "\nIf this strategic approach didn't trigger the crash, the race condition may require:" << std::endl;
        std::cout << "1. Very specific hardware timing conditions" << std::endl;
        std::cout << "2. Multiple simultaneous instances for increased contention" << std::endl;
        std::cout << "3. Specific UDF filesystem state or fragmentation" << std::endl;
        std::cout << "4. The original bug may have been a very rare edge case" << std::endl;
        std::cout << "5. Different VM/hardware configuration with different I/O timing" << std::endl;
    }
    
    // Cleanup any leftover test files
    void Cleanup() {
        std::cout << "Cleaning up test files..." << std::endl;
        
        WIN32_FIND_DATAA findData;
        std::string searchPattern = testPath + "stress_*.tmp";
        HANDLE hFind = FindFirstFileA(searchPattern.c_str(), &findData);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                std::string fullPath = testPath + findData.cFileName;
                DeleteFileA(fullPath.c_str());
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
        }
        
        // Also clean race, overflow, pressure, and attr files
        std::vector<std::string> patterns = {"race_*.tmp", "overflow_*.tmp", "pressure_*.tmp", "attr_*.tmp"};
        for (const auto& pattern : patterns) {
            searchPattern = testPath + pattern;
            hFind = FindFirstFileA(searchPattern.c_str(), &findData);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    std::string fullPath = testPath + findData.cFileName;
                    DeleteFileA(fullPath.c_str());
                } while (FindNextFileA(hFind, &findData));
                FindClose(hFind);
            }
        }
    }
};

int main(int argc, char* argv[]) {
#ifdef LINUX_DEMO
    std::cout << "UDFS Driver REAL KERNEL CRASH Test (Linux Demo Mode)" << std::endl;
#else
    std::cout << "UDFS Driver REAL KERNEL CRASH Test" << std::endl;
#endif
    std::cout << "===================================" << std::endl;
    std::cout << "This program attempts to trigger the actual UDFS driver race condition" << std::endl;
    std::cout << "that causes UNEXPECTED_KERNEL_MODE_TRAP (0x7F) BSOD." << std::endl;
    
    std::string testPath;
    if (argc > 1) {
        testPath = argv[1];
    } else {
        std::cout << "\nUsage: " << argv[0] << " <path_to_udf_drive>" << std::endl;
#ifdef LINUX_DEMO
        std::cout << "Example (Windows): " << argv[0] << " D:\\" << std::endl;
        std::cout << "Example (Linux demo): " << argv[0] << " /tmp/udfs_test/" << std::endl;
#else
        std::cout << "Example: " << argv[0] << " D:\\" << std::endl;
#endif
        std::cout << "\nEnter path to UDF drive (or press Enter for default): ";
        std::getline(std::cin, testPath);
        if (testPath.empty()) {
#ifdef LINUX_DEMO
            testPath = "/tmp/udfs_test/";
            mkdir(testPath.c_str(), 0755);
#else
            testPath = "C:\\temp\\";
            // Create directory if it doesn't exist
            CreateDirectoryA(testPath.c_str(), nullptr);
#endif
        }
    }
    
    try {
        UDFSCrashTrigger crashTest(testPath);
        crashTest.RunKernelCrashTest();
        crashTest.Cleanup();
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
