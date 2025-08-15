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
 *   RosBE MinGW: gcc -o udfs_crash_repro.exe udfs_crash_repro.cpp
 *   Alternative:  g++ -o udfs_crash_repro.exe udfs_crash_repro.cpp
 *   With linking: gcc -o udfs_crash_repro.exe udfs_crash_repro.cpp -lstdc++
 *   Cross-compile: i686-w64-mingw32-gcc -o udfs_crash_repro.exe udfs_crash_repro.cpp
 *   Linux (demo): gcc -o udfs_crash_repro udfs_crash_repro.cpp -DLINUX_DEMO -lpthread
 * 
 * Run with: ./udfs_crash_repro [path_to_udf_drive]
 */

#ifdef LINUX_DEMO
// Linux demo version - shows what the program would do on Windows
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>

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
    printf("[MOCK] CreateFile: %s (flags: 0x%lx)\n", filename, (unsigned long)flags);
    return (HANDLE)1; // Fake success
}

int WriteFile(HANDLE file, const void* buffer, DWORD size, DWORD* written, void* overlapped) {
    *written = size;
    printf("[MOCK] WriteFile: %lu bytes\n", (unsigned long)size);
    return 1;
}

int ReadFile(HANDLE file, void* buffer, DWORD size, DWORD* read, void* overlapped) {
    *read = size;
    printf("[MOCK] ReadFile: %lu bytes\n", (unsigned long)size);
    return 1;
}

int FlushFileBuffers(HANDLE file) {
    printf("[MOCK] FlushFileBuffers\n");
    return 1;
}

int CloseHandle(HANDLE handle) {
    printf("[MOCK] CloseHandle\n");
    return 1;
}

int DeleteFileA(const char* filename) {
    printf("[MOCK] DeleteFile: %s\n", filename);
    return 1;
}

DWORD SetFilePointer(HANDLE file, long distance, long* high, DWORD method) {
    printf("[MOCK] SetFilePointer\n");
    return 0;
}

int GetVolumeInformationA(const char* root, char* volume, DWORD vol_size, DWORD* serial, DWORD* max_len, DWORD* flags, char* fs_name, DWORD fs_size) {
    strcpy(fs_name, "ext4");  // Simulate non-UDF filesystem on Linux
    strcpy(volume, "Linux Demo");
    return 1;
}

int CreateDirectoryA(const char* path, void* security) {
    mkdir(path, 0755);
    return 1;
}

int RemoveDirectoryA(const char* path) {
    printf("[MOCK] RemoveDirectory: %s\n", path);
    rmdir(path);
    return 1;
}

HANDLE FindFirstFileA(const char* pattern, WIN32_FIND_DATAA* data) {
    printf("[MOCK] FindFirstFile: %s\n", pattern);
    return INVALID_HANDLE_VALUE; // No files found
}

int FindNextFileA(HANDLE find, WIN32_FIND_DATAA* data) { return 0; }
int FindClose(HANDLE find) { return 1; }

// Mock threading functions
void Sleep(DWORD milliseconds) {
    usleep(milliseconds * 1000);
}

#else
// Real Windows implementation
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#endif

// Thread-safe variables using mutex instead of atomic
#ifndef LINUX_DEMO
static CRITICAL_SECTION g_stopMutex;
static CRITICAL_SECTION g_threadCountMutex;
#else
static pthread_mutex_t g_stopMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_threadCountMutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static int g_shouldStop = 0;
static int g_activeThreads = 0;

// Thread-safe getter/setter functions
int GetShouldStop() {
#ifndef LINUX_DEMO
    EnterCriticalSection(&g_stopMutex);
    int result = g_shouldStop;
    LeaveCriticalSection(&g_stopMutex);
#else
    pthread_mutex_lock(&g_stopMutex);
    int result = g_shouldStop;
    pthread_mutex_unlock(&g_stopMutex);
#endif
    return result;
}

void SetShouldStop(int value) {
#ifndef LINUX_DEMO
    EnterCriticalSection(&g_stopMutex);
    g_shouldStop = value;
    LeaveCriticalSection(&g_stopMutex);
#else
    pthread_mutex_lock(&g_stopMutex);
    g_shouldStop = value;
    pthread_mutex_unlock(&g_stopMutex);
#endif
}

int GetActiveThreads() {
#ifndef LINUX_DEMO
    EnterCriticalSection(&g_threadCountMutex);
    int result = g_activeThreads;
    LeaveCriticalSection(&g_threadCountMutex);
#else
    pthread_mutex_lock(&g_threadCountMutex);
    int result = g_activeThreads;
    pthread_mutex_unlock(&g_threadCountMutex);
#endif
    return result;
}

void IncrementActiveThreads() {
#ifndef LINUX_DEMO
    EnterCriticalSection(&g_threadCountMutex);
    g_activeThreads++;
    LeaveCriticalSection(&g_threadCountMutex);
#else
    pthread_mutex_lock(&g_threadCountMutex);
    g_activeThreads++;
    pthread_mutex_unlock(&g_threadCountMutex);
#endif
}

void DecrementActiveThreads() {
#ifndef LINUX_DEMO
    EnterCriticalSection(&g_threadCountMutex);
    g_activeThreads--;
    LeaveCriticalSection(&g_threadCountMutex);
#else
    pthread_mutex_lock(&g_threadCountMutex);
    g_activeThreads--;
    pthread_mutex_unlock(&g_threadCountMutex);
#endif
}

// Portable sleep function
void PortableSleep(int milliseconds) {
#ifndef LINUX_DEMO
    Sleep(milliseconds);
#else
    Sleep(milliseconds);
#endif
}

void PortableMicroSleep(int microseconds) {
#ifndef LINUX_DEMO
    Sleep(microseconds / 1000);
#else
    usleep(microseconds);
#endif
}

// Thread data structure
struct ThreadData {
    int threadId;
    char* testPath;
};

// Strategic test parameters designed to target specific race condition timing
static const int NUM_WORKER_THREADS = 12;     // Moderate thread count for optimal race conditions
static const int FILES_PER_THREAD = 30;       // Focused file burst per iteration
static const int MAX_FILE_SIZE = 64 * 1024;   // Moderate file sizes (64KB) for realistic I/O
static const int MIN_FILE_SIZE = 512;         // Reasonable minimum (512B) for proper I/O timing
static const int ITERATION_COUNT = 1000;      // Sufficient iterations without overwhelming
static const int STRATEGIC_BURST_SIZE = 25;   // Strategic burst operations with timing gaps
static const int OVERFLOW_STRESS_CYCLES = 200; // Focused overflow queue stress cycles
static const int RACE_CONDITION_THREADS = 4;  // Optimal threads for race condition timing

// Generate random data for file operations (caller must free the returned pointer)
char* GenerateRandomData(size_t size) {
    char* data = (char*)malloc(size);
    if (!data) return NULL;
    
    srand((unsigned int)time(NULL) + GetActiveThreads()); // Seed with time + thread variation
    
    for (size_t i = 0; i < size; ++i) {
        data[i] = (char)(rand() % 256);
    }
    return data;
}

// Generate random number in range
int RandomInRange(int min, int max) {
    return min + (rand() % (max - min + 1));
}

// Convert integer to string
void IntToString(int value, char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%d", value);
}

// Check if we're running on a UDF filesystem
int IsUDFFilesystem(const char* testPath) {
    char volumeName[260]; // MAX_PATH equivalent
    char fsName[260];
    DWORD serialNumber, maxComponentLength, fsFlags;
    
#ifdef LINUX_DEMO
    printf("\n=== LINUX DEMO MODE ===\n");
    printf("This is a demonstration of what the program would do on Windows/ReactOS.\n");
    printf("On a real UDF filesystem with unfixed driver, this would trigger kernel crash.\n");
    printf("========================\n");
#endif
    
    char rootPath[4];
    strncpy(rootPath, testPath, 3);
    rootPath[3] = '\0';  // Extract "C:\" style root
    
    if (GetVolumeInformationA(
        rootPath,
        volumeName, sizeof(volumeName),
        &serialNumber,
        &maxComponentLength,
        &fsFlags,
        fsName, sizeof(fsName))) {
        
        printf("Filesystem: %s\n", fsName);
        printf("Volume: %s\n", volumeName);
        
#ifdef LINUX_DEMO
        printf("\n[LINUX DEMO] On ReactOS with UDF filesystem, this would show 'UDF' instead of '%s'\n", fsName);
        return 0; // Simulate non-UDF for demo
#else
        // Check if it's UDF
        if (strstr(fsName, "UDF") != NULL) {
            printf("✓ UDF filesystem detected!\n");
            return 1;
        } else {
            printf("⚠ Warning: Not a UDF filesystem. This test may not trigger the bug.\n");
            printf("  For best results, run on a UDF 2.01 formatted drive.\n");
            return 0;
        }
#endif
    }
    
    printf("⚠ Warning: Could not determine filesystem type.\n");
    return 0;
}

// File stress worker thread function
#ifndef LINUX_DEMO
DWORD WINAPI FileStressWorkerThread(LPVOID param) {
#else
void* FileStressWorkerThread(void* param) {
#endif
    struct ThreadData* data = (struct ThreadData*)param;
    int threadId = data->threadId;
    char* testPath = data->testPath;
    
    IncrementActiveThreads();
    
    // Set thread to idle priority to reduce system impact
#ifndef LINUX_DEMO
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
#else
    printf("[MOCK] SetThreadPriority(THREAD_PRIORITY_IDLE)\n");
#endif
    
    printf("Thread %d started (idle priority)\n", threadId);
    
    for (int iteration = 0; iteration < ITERATION_COUNT && !GetShouldStop(); ++iteration) {
        char createdFiles[STRATEGIC_BURST_SIZE][1024];
        int createdFileCount = 0;
        
        // Phase 1: Strategic burst file creation with controlled timing
        int burstSize = RandomInRange(5, STRATEGIC_BURST_SIZE);
        for (int i = 0; i < burstSize; ++i) {
            if (GetShouldStop()) break;
            
            char filename[1024];
            char threadIdStr[32], iterationStr[32], iStr[32];
            IntToString(threadId, threadIdStr, sizeof(threadIdStr));
            IntToString(iteration, iterationStr, sizeof(iterationStr));
            IntToString(i, iStr, sizeof(iStr));
            
            snprintf(filename, sizeof(filename), "%sstress_t%s_i%s_f%s.tmp", 
                    testPath, threadIdStr, iterationStr, iStr);
            
            size_t fileSize = RandomInRange(MIN_FILE_SIZE, MAX_FILE_SIZE);
            char* fileData = GenerateRandomData(fileSize);
            if (!fileData) continue;
            
            // Create file with strategic flags for race condition timing
            HANDLE hFile = CreateFileA(
                filename,
                GENERIC_WRITE | GENERIC_READ,
                FILE_SHARE_READ,  // Allow sharing to reduce extreme contention
                NULL,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, // Removed NO_BUFFERING for better timing
                NULL
            );
            
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD bytesWritten;
                WriteFile(hFile, fileData, fileSize, &bytesWritten, NULL);
                FlushFileBuffers(hFile); // Force immediate flush
                
                // Strategic read-back with timing control
                SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
                char* readBuffer = (char*)malloc(fileSize);
                if (readBuffer) {
                    DWORD bytesRead;
                    ReadFile(hFile, readBuffer, fileSize, &bytesRead, NULL);
                    free(readBuffer);
                }
                
                CloseHandle(hFile);
                
                // Store filename for later deletion
                if (createdFileCount < STRATEGIC_BURST_SIZE) {
                    strcpy(createdFiles[createdFileCount], filename);
                    createdFileCount++;
                }
            }
            
            free(fileData);
            
            // Strategic micro-delay to control I/O timing for race conditions
            PortableMicroSleep(200);
        }
        
        // Phase 2: Strategic concurrent file operations to create controlled race conditions
        for (int i = 0; i < 6 && !GetShouldStop(); ++i) { // Reduced from 16 to 6 concurrent operations
            char concurrentFile[1024];
            char threadIdStr[32], iterationStr[32], iStr[32];
            IntToString(threadId, threadIdStr, sizeof(threadIdStr));
            IntToString(iteration, iterationStr, sizeof(iterationStr));
            IntToString(i, iStr, sizeof(iStr));
            
            snprintf(concurrentFile, sizeof(concurrentFile), "%srace_t%s_i%s_r%s.tmp", 
                    testPath, threadIdStr, iterationStr, iStr);
            
            // Strategic rapid create/delete cycles with controlled timing
            for (int cycle = 0; cycle < 25 && !GetShouldStop(); ++cycle) { // Reduced from 100 to 25 cycles
                HANDLE hFile = CreateFileA(
                    concurrentFile,
                    GENERIC_WRITE | GENERIC_READ,
                    FILE_SHARE_READ, // Reduced contention
                    NULL,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, // Removed DELETE_ON_CLOSE and NO_BUFFERING
                    NULL
                );
                
                if (hFile != INVALID_HANDLE_VALUE) {
                    char* fileData = GenerateRandomData(1024); // Fixed size for consistency
                    if (fileData) {
                        DWORD written;
                        WriteFile(hFile, fileData, 1024, &written, NULL);
                        free(fileData);
                    }
                    
                    // Strategic file manipulation with timing control
                    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
                    PortableMicroSleep(100);
                    SetFilePointer(hFile, 512, NULL, FILE_BEGIN);
                    PortableMicroSleep(100);
                    SetFilePointer(hFile, 0, NULL, FILE_END);
                    
                    FlushFileBuffers(hFile);
                    
                    // Read operations with strategic timing
                    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
                    char* readBuf = (char*)malloc(1024);
                    if (readBuf) {
                        DWORD read;
                        ReadFile(hFile, readBuf, 1024, &read, NULL);
                        free(readBuf);
                    }
                    
                    CloseHandle(hFile);
                }
                
                // Strategic delay before deletion to create race timing window
                PortableMicroSleep(300);
                DeleteFileA(concurrentFile);
                
                // Strategic delay between cycles to allow proper timing
                PortableSleep(1);
            }
        }
        
        // Phase 3: Strategic deletion burst to trigger the race condition
        for (int j = 0; j < createdFileCount; ++j) {
            if (GetShouldStop()) break;
            DeleteFileA(createdFiles[j]);
            PortableMicroSleep(100); // Strategic micro-delay
        }
        
        if (iteration % 25 == 0) {
            printf("Thread %d completed %d iterations (burst size: %d)\n", threadId, iteration, burstSize);
        }
        
        // Strategic delay between iterations to prevent CPU saturation
        PortableSleep(5);
    }
    
    printf("Thread %d finished\n", threadId);
    DecrementActiveThreads();
    
#ifndef LINUX_DEMO
    return 0;
#else
    return NULL;
#endif
}

// Main stress test that tries to trigger the UDFS race condition
void RunKernelCrashTest(const char* testPath) {
    printf("\n===========================================\n");
    printf("UDFS KERNEL CRASH TEST\n");
    printf("===========================================\n");
    printf("⚠️  WARNING: THIS TEST IS DESIGNED TO CRASH THE KERNEL!\n");
    printf("⚠️  SAVE ALL YOUR WORK BEFORE RUNNING!\n");
    printf("===========================================\n");
    
    printf("Test path: %s\n", testPath);
    IsUDFFilesystem(testPath);
    
    printf("\nStarting strategic stress test with:\n");
    printf("- %d file worker threads (optimized for race conditions, idle priority)\n", NUM_WORKER_THREADS);
    printf("- %d files per thread per iteration (strategic load)\n", FILES_PER_THREAD);
    printf("- %d iterations per thread (sufficient coverage)\n", ITERATION_COUNT);
    printf("- File sizes: %d - %d bytes (realistic I/O timing)\n", MIN_FILE_SIZE, MAX_FILE_SIZE);
    printf("- %d max burst operations per iteration (controlled)\n", STRATEGIC_BURST_SIZE);
    printf("- %d race condition threads per worker (optimal timing)\n", RACE_CONDITION_THREADS);
    
    printf("\nPress Enter to start the strategic test designed to trigger the race condition...\n");
#ifdef LINUX_DEMO
    printf("[LINUX DEMO] In real ReactOS, this would attempt to trigger the kernel crash!\n");
#endif
    getchar();
    
    printf("\nStarting strategic kernel crash test...\n");
    
    // Initialize critical sections
#ifndef LINUX_DEMO
    InitializeCriticalSection(&g_stopMutex);
    InitializeCriticalSection(&g_threadCountMutex);
#endif
    
    // Create thread data structures - allocate dynamically to avoid dangling pointers
    struct ThreadData* threadDataList = (struct ThreadData*)malloc(NUM_WORKER_THREADS * sizeof(struct ThreadData));
    if (!threadDataList) {
        printf("Failed to allocate memory for thread data\n");
        return;
    }
    
#ifndef LINUX_DEMO
    HANDLE* workerHandles = (HANDLE*)malloc(NUM_WORKER_THREADS * sizeof(HANDLE));
#else
    pthread_t* workerHandles = (pthread_t*)malloc(NUM_WORKER_THREADS * sizeof(pthread_t));
#endif
    
    if (!workerHandles) {
        printf("Failed to allocate memory for thread handles\n");
        free(threadDataList);
        return;
    }
    
    // Start file stress workers
    for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
        threadDataList[i].threadId = i;
        threadDataList[i].testPath = (char*)testPath;
        
#ifndef LINUX_DEMO
        workerHandles[i] = CreateThread(NULL, 0, FileStressWorkerThread, &threadDataList[i], 0, NULL);
#else
        pthread_create(&workerHandles[i], NULL, FileStressWorkerThread, &threadDataList[i]);
#endif
    }
    
    printf("Total threads launched: %d\n", NUM_WORKER_THREADS);
    
    // Monitor progress and show status with better timing
    clock_t startTime = clock();
    while (GetActiveThreads() > 0) {
        PortableSleep(15000); // Check every 15 seconds (less frequent)
        clock_t elapsed = (clock() - startTime) / CLOCKS_PER_SEC;
        printf("Active threads: %d, Elapsed: %lds\n", GetActiveThreads(), elapsed);
        printf("    STATUS: Strategic stress with %d total threads targeting UDFS race condition...\n", NUM_WORKER_THREADS);
        
        // Extended timeout to 10 minutes since we're using strategic timing
        if (elapsed > 600) {
            printf("\nStrategic stress running for 10+ minutes without crash. This suggests:\n");
            printf("1. Race condition may require very specific timing/hardware conditions\n");
            printf("2. Driver may be fixed or environment doesn't reproduce the issue\n");
            printf("3. UDFS overflow queue may be handling strategic stress without race condition\n");
            printf("4. Consider running on different hardware/VM configuration\n");
            printf("5. Try running multiple instances simultaneously for increased contention\n");
            SetShouldStop(1);
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
#ifndef LINUX_DEMO
        WaitForSingleObject(workerHandles[i], INFINITE);
        CloseHandle(workerHandles[i]);
#else
        pthread_join(workerHandles[i], NULL);
#endif
    }
    
    // Clean up
    free(threadDataList);
    free(workerHandles);
    
#ifndef LINUX_DEMO
    DeleteCriticalSection(&g_stopMutex);
    DeleteCriticalSection(&g_threadCountMutex);
#endif
    
    printf("\nStrategic stress test completed without kernel crash.\n");
    printf("This strategic version used optimized techniques for race condition targeting:\n");
    printf("- %d file threads (all idle priority)\n", NUM_WORKER_THREADS);
    printf("- %d files per burst, %d iterations, %d burst size\n", FILES_PER_THREAD, ITERATION_COUNT, STRATEGIC_BURST_SIZE);
    printf("- %dB-%dKB files (realistic I/O timing), strategic micro-delays\n", MIN_FILE_SIZE, MAX_FILE_SIZE/1024);
    printf("- 6 concurrent operations per worker, 25 cycles each\n");
    printf("- Strategic timing delays, controlled file sharing\n");
    printf("- Idle priority threads for minimal system impact and better responsiveness\n");
    printf("- Focused race condition targeting with proper timing\n");
}

// Cleanup any leftover test files
void Cleanup(const char* testPath) {
    printf("Cleaning up test files...\n");
    
    WIN32_FIND_DATAA findData;
    char searchPattern[1024];
    
    const char* patterns[] = {"stress_*.tmp", "race_*.tmp", "overflow_*.tmp", "pressure_*.tmp", "attr_*.tmp"};
    for (int p = 0; p < 5; ++p) {
        snprintf(searchPattern, sizeof(searchPattern), "%s%s", testPath, patterns[p]);
        HANDLE hFind = FindFirstFileA(searchPattern, &findData);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                char fullPath[1024];
                snprintf(fullPath, sizeof(fullPath), "%s%s", testPath, findData.cFileName);
                DeleteFileA(fullPath);
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
        }
    }
}

int main(int argc, char* argv[]) {
#ifdef LINUX_DEMO
    printf("UDFS Driver REAL KERNEL CRASH Test (Linux Demo Mode)\n");
#else
    printf("UDFS Driver REAL KERNEL CRASH Test\n");
#endif
    printf("===================================\n");
    printf("This program attempts to trigger the actual UDFS driver race condition\n");
    printf("that causes UNEXPECTED_KERNEL_MODE_TRAP (0x7F) BSOD.\n");
    
    char testPath[1024];
    if (argc > 1) {
        strncpy(testPath, argv[1], sizeof(testPath) - 1);
        testPath[sizeof(testPath) - 1] = '\0';
    } else {
        printf("\nUsage: %s <path_to_udf_drive>\n", argv[0]);
#ifdef LINUX_DEMO
        printf("Example (Windows): %s D:\\\n", argv[0]);
        printf("Example (Linux demo): %s /tmp/udfs_test/\n", argv[0]);
#else
        printf("Example: %s D:\\\n", argv[0]);
#endif
        printf("\nEnter path to UDF drive (or press Enter for default): ");
        if (!fgets(testPath, sizeof(testPath), stdin) || testPath[0] == '\n') {
#ifdef LINUX_DEMO
            strcpy(testPath, "/tmp/udfs_test/");
            mkdir(testPath, 0755);
#else
            strcpy(testPath, "C:\\temp\\");
            // Create directory if it doesn't exist
            CreateDirectoryA(testPath, NULL);
#endif
        } else {
            // Remove newline from fgets
            size_t len = strlen(testPath);
            if (len > 0 && testPath[len - 1] == '\n') {
                testPath[len - 1] = '\0';
            }
        }
    }
    
    // Ensure path ends with backslash/slash
    size_t len = strlen(testPath);
    if (len > 0 && testPath[len - 1] != '\\' && testPath[len - 1] != '/') {
        if (len < sizeof(testPath) - 2) {
            strcat(testPath, "\\");
        }
    }
    
    RunKernelCrashTest(testPath);
    Cleanup(testPath);
    
    return 0;
}