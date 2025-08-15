/*
 * UDFS Driver REAL KERNEL CRASH Test Program (Steam Download Simulator)
 * 
 * WARNING: THIS PROGRAM IS DESIGNED TO CRASH THE KERNEL!
 * Only run this if you want to trigger the actual UDFS driver BSOD for debugging purposes.
 * 
 * This program triggers the real race condition in the ReactOS UDFS driver that causes:
 * UNEXPECTED_KERNEL_MODE_TRAP (0x7F) - when RemoveHeadList() is called on corrupted list
 * 
 * ENHANCED v2: Aggressively targets UDFS race condition with overlapping operations:
 * - 24 high-priority threads for maximum concurrency stress
 * - Mixed small (4KB-256KB) and large (1-100MB) files to stress different code paths
 * - Overlapping create/write/read/delete operations (no sequential phases)
 * - Directory operations mixed with file operations for metadata stress
 * - Random access patterns mixed with sequential writes
 * - Multiple concurrent file handles per thread
 * - Above-normal thread priority for aggressive timing
 * - No delays between operations for maximum race condition potential
 * - Immediate flush with no buffering flags for filesystem stress
 * - Extended runtime (45+ minutes) for sustained race condition targeting
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
const DWORD OPEN_EXISTING = 3;
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

// Enhanced parameters designed to trigger UDFS race condition more aggressively
static const int NUM_WORKER_THREADS = 24;         // High thread count to maximize concurrent operations
static const int FILES_PER_THREAD = 15;           // More files per thread for increased stress
static const int MAX_FILE_SIZE = 100 * 1024 * 1024; // Reduced max size for faster operations
static const int MIN_FILE_SIZE = 4 * 1024;        // Mix small files (4KB) with large ones
static const int ITERATION_COUNT = 200;           // More iterations for sustained stress
static const int CHUNK_SIZE = 32 * 1024;          // Smaller chunks for more frequent I/O calls
static const int STEAM_PROGRESS_INTERVAL = 5;     // More frequent progress updates
static const int CONCURRENT_DOWNLOADS = 8;        // More concurrent downloads
static const int SMALL_FILE_RATIO = 4;            // 1 in 4 files will be small (to mix patterns)
static const int DIRECTORY_OPERATIONS_PER_THREAD = 10; // Add directory stress

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

// Generate random file size with mix of small and large files
size_t GenerateRandomFileSize() {
    // 25% chance of small file (4KB-256KB), 75% chance of large file (1MB-100MB)
    if (rand() % SMALL_FILE_RATIO == 0) {
        return RandomInRange(MIN_FILE_SIZE, 256 * 1024); // Small files: 4KB-256KB
    } else {
        return RandomInRange(1024 * 1024, MAX_FILE_SIZE); // Large files: 1MB-100MB
    }
}

// Aggressive random access pattern to stress filesystem
int AggressiveRandomAccess(HANDLE hFile, size_t fileSize) {
    const int NUM_RANDOM_SEEKS = 20;
    char buffer[4096];
    DWORD bytesRead;
    
    for (int i = 0; i < NUM_RANDOM_SEEKS; ++i) {
        // Random seek within file
        DWORD seekPos = RandomInRange(0, (int)(fileSize > sizeof(buffer) ? fileSize - sizeof(buffer) : 0));
        SetFilePointer(hFile, seekPos, NULL, FILE_BEGIN);
        
        // Random read
        ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL);
        
        // No delay - make it as aggressive as possible
    }
    
    return 1;
}

// Convert integer to string
void IntToString(int value, char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%d", value);
}

// Steam-like game file names (common Steam game file patterns)
const char* STEAM_GAME_FILES[] = {
    "gamedata.pak",
    "engine.dll", 
    "assets.bundle",
    "textures.archive",
    "sounds.bank",
    "models.mesh",
    "scripts.bin",
    "config.cfg"
};
const int STEAM_GAME_FILES_COUNT = 8;

// Generate Steam-like filename 
void GenerateSteamFileName(int threadId, int fileIndex, char* buffer, size_t bufferSize) {
    const char* baseFile = STEAM_GAME_FILES[fileIndex % STEAM_GAME_FILES_COUNT];
    snprintf(buffer, bufferSize, "game_%d_%s", threadId, baseFile);
}

// Steam-like download progress tracking
void ShowDownloadProgress(const char* filename, size_t downloaded, size_t total) {
    int percent = (int)((downloaded * 100) / total);
    printf("[STEAM] Downloading %s: %d%% (%zu/%zu bytes)\n", 
           filename, percent, downloaded, total);
}

// Simulate Steam's chunked download writing - Enhanced for race condition triggering
int SteamChunkedDownload(HANDLE hFile, const char* filename, size_t totalSize) {
    size_t downloaded = 0;
    int chunkCount = 0;
    
    while (downloaded < totalSize) {
        size_t chunkSize = CHUNK_SIZE;
        if (downloaded + chunkSize > totalSize) {
            chunkSize = totalSize - downloaded;
        }
        
        // Generate chunk data (simulating downloaded content)
        char* chunkData = GenerateRandomData(chunkSize);
        if (!chunkData) {
            return 0;
        }
        
        // Write chunk to file (simulating Steam's streaming write)
        DWORD bytesWritten;
        if (!WriteFile(hFile, chunkData, chunkSize, &bytesWritten, NULL)) {
            free(chunkData);
            return 0;
        }
        
        // Aggressive flush - no buffering to stress filesystem immediately
        FlushFileBuffers(hFile);
        
        // Random access pattern mixed in to stress different code paths
        if (chunkCount % 3 == 0 && downloaded > CHUNK_SIZE) {
            AggressiveRandomAccess(hFile, downloaded);
        }
        
        downloaded += bytesWritten;
        chunkCount++;
        
        // Show progress more frequently
        if (chunkCount % STEAM_PROGRESS_INTERVAL == 0) {
            ShowDownloadProgress(filename, downloaded, totalSize);
        }
        
        free(chunkData);
        
        // Reduced delay for more aggressive timing
        if (totalSize > 1024 * 1024) {
            // No delay for large files - maximum aggression
        } else {
            // Tiny delay for small files to allow some overlap
            PortableMicroSleep(100);
        }
    }
    
    return 1;
}

// Steam-like file validation (simulate checksum verification)
int ValidateDownloadedFile(HANDLE hFile, const char* filename) {
    printf("[STEAM] Validating %s...\n", filename);
    
    // Simulate validation by reading the file back
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    
    char buffer[CHUNK_SIZE];
    DWORD bytesRead;
    size_t totalValidated = 0;
    
    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        totalValidated += bytesRead;
        // Small delay to simulate checksum calculation
        PortableMicroSleep(100);
    }
    
    printf("[STEAM] Validation complete: %zu bytes verified\n", totalValidated);
    return 1;
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

// Aggressive directory operations to stress filesystem metadata
void AggressiveDirectoryOperations(const char* testPath, int threadId) {
    for (int i = 0; i < DIRECTORY_OPERATIONS_PER_THREAD; ++i) {
        char dirName[1024];
        snprintf(dirName, sizeof(dirName), "%sdir_%d_%d", testPath, threadId, i);
        
        // Create directory
        CreateDirectoryA(dirName, NULL);
        
        // Create and delete files inside directory rapidly
        for (int j = 0; j < 5; ++j) {
            char subFile[1024];
            snprintf(subFile, sizeof(subFile), "%s\\temp_%d.tmp", dirName, j);
            
            HANDLE hSubFile = CreateFileA(
                subFile, GENERIC_WRITE, 0, NULL, 
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
            
            if (hSubFile != INVALID_HANDLE_VALUE) {
                // Write small amount of data
                char data[1024] = "temporary data";
                DWORD bytesWritten;
                WriteFile(hSubFile, data, sizeof(data), &bytesWritten, NULL);
                FlushFileBuffers(hSubFile);
                CloseHandle(hSubFile);
                
                // Immediately delete - stress metadata operations
                DeleteFileA(subFile);
            }
        }
        
        // Remove directory (stress metadata further)
        RemoveDirectoryA(dirName);
        
        // No delay - maximum aggression
    }
}

// Steam download simulation worker thread function - Enhanced for race condition
#ifndef LINUX_DEMO
DWORD WINAPI FileStressWorkerThread(LPVOID param) {
#else
void* FileStressWorkerThread(void* param) {
#endif
    struct ThreadData* data = (struct ThreadData*)param;
    int threadId = data->threadId;
    char* testPath = data->testPath;
    
    IncrementActiveThreads();
    
    // Set thread to ABOVE_NORMAL priority for more aggressive timing
#ifndef LINUX_DEMO
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#else
    printf("[MOCK] SetThreadPriority(THREAD_PRIORITY_ABOVE_NORMAL)\n");
#endif
    
    printf("[RACE] Aggressive thread %d started (targeting UDFS race condition)\n", threadId);
    
    // Pre-allocate file handle arrays for overlapping operations
    const int MAX_CONCURRENT_FILES = 10;
    HANDLE activeFiles[MAX_CONCURRENT_FILES];
    char activeFilenames[MAX_CONCURRENT_FILES][1024];
    for (int i = 0; i < MAX_CONCURRENT_FILES; ++i) {
        activeFiles[i] = INVALID_HANDLE_VALUE;
    }
    
    for (int iteration = 0; iteration < ITERATION_COUNT && !GetShouldStop(); ++iteration) {
        // Phase 0: Aggressive directory operations mixed with file operations
        if (iteration % 3 == 0) {
            AggressiveDirectoryOperations(testPath, threadId);
        }
        
        // Phase 1: Create multiple files concurrently (overlapping operations)
        for (int fileIndex = 0; fileIndex < FILES_PER_THREAD && !GetShouldStop(); ++fileIndex) {
            int slotIndex = fileIndex % MAX_CONCURRENT_FILES;
            
            // Close previous file in this slot if still open
            if (activeFiles[slotIndex] != INVALID_HANDLE_VALUE) {
                CloseHandle(activeFiles[slotIndex]);
                activeFiles[slotIndex] = INVALID_HANDLE_VALUE;
            }
            
            char downloadingFile[1024];
            char finalFile[1024];
            GenerateSteamFileName(threadId, fileIndex, finalFile, sizeof(finalFile));
            snprintf(downloadingFile, sizeof(downloadingFile), "%s%s.downloading", testPath, finalFile);
            snprintf(finalFile, sizeof(finalFile), "%s%s", testPath, finalFile);
            
            // Store filename for later operations
            strncpy(activeFilenames[slotIndex], downloadingFile, sizeof(activeFilenames[slotIndex]) - 1);
            activeFilenames[slotIndex][sizeof(activeFilenames[slotIndex]) - 1] = '\0';
            
            size_t fileSize = GenerateRandomFileSize();
            printf("[RACE] Thread %d creating: %s (%zu bytes) [slot %d]\n", 
                   threadId, downloadingFile, fileSize, slotIndex);
            
            // Create downloading file with aggressive flags for immediate write-through
            activeFiles[slotIndex] = CreateFileA(
                downloadingFile,
                GENERIC_WRITE | GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, // Allow concurrent access
                NULL,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING,
                NULL
            );
            
            if (activeFiles[slotIndex] == INVALID_HANDLE_VALUE) {
                printf("[RACE] Failed to create file: %s\n", downloadingFile);
                continue;
            }
            
            // Start download in background while creating more files
            if (fileSize > 50 * 1024 * 1024) {
                // For large files, start download but don't wait - overlap operations
                SteamChunkedDownload(activeFiles[slotIndex], downloadingFile, fileSize > 10*1024*1024 ? 10*1024*1024 : fileSize);
            } else {
                // For small files, complete immediately
                SteamChunkedDownload(activeFiles[slotIndex], downloadingFile, fileSize);
            }
            
            // No delay between file creations - maximum overlap
        }
        
        // Phase 2: Aggressive overlapping validation while creating new files
        for (int slotIndex = 0; slotIndex < MAX_CONCURRENT_FILES && !GetShouldStop(); ++slotIndex) {
            if (activeFiles[slotIndex] != INVALID_HANDLE_VALUE) {
                // Random access validation while other operations are happening
                DWORD fileSize = SetFilePointer(activeFiles[slotIndex], 0, NULL, FILE_END);
                if (fileSize > 0) {
                    AggressiveRandomAccess(activeFiles[slotIndex], fileSize);
                }
            }
        }
        
        // Phase 3: Rapid atomic operations (rename, delete) while files are still open
        for (int fileIndex = 0; fileIndex < FILES_PER_THREAD && !GetShouldStop(); ++fileIndex) {
            int slotIndex = fileIndex % MAX_CONCURRENT_FILES;
            
            if (activeFiles[slotIndex] != INVALID_HANDLE_VALUE) {
                char finalFile[1024];
                GenerateSteamFileName(threadId, fileIndex, finalFile, sizeof(finalFile));
                snprintf(finalFile, sizeof(finalFile), "%s%s", testPath, finalFile);
                
                // Close the downloading file
                CloseHandle(activeFiles[slotIndex]);
                activeFiles[slotIndex] = INVALID_HANDLE_VALUE;
                
                // Rapid rename operation (stress metadata)
                DeleteFileA(finalFile); // Remove if exists
                
                // Simulate rename by copying (more filesystem stress than Windows rename)
                HANDLE hSource = CreateFileA(activeFilenames[slotIndex], GENERIC_READ, 
                                           FILE_SHARE_READ, NULL, OPEN_EXISTING, 
                                           FILE_ATTRIBUTE_NORMAL, NULL);
                if (hSource != INVALID_HANDLE_VALUE) {
                    HANDLE hDest = CreateFileA(finalFile, GENERIC_WRITE, 0, NULL, 
                                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
                    if (hDest != INVALID_HANDLE_VALUE) {
                        char copyBuffer[4096];
                        DWORD bytesRead, bytesWritten;
                        while (ReadFile(hSource, copyBuffer, sizeof(copyBuffer), &bytesRead, NULL) && bytesRead > 0) {
                            WriteFile(hDest, copyBuffer, bytesRead, &bytesWritten, NULL);
                            FlushFileBuffers(hDest); // Stress the filesystem
                        }
                        CloseHandle(hDest);
                    }
                    CloseHandle(hSource);
                }
                
                // Immediately delete both files (stress metadata operations)
                DeleteFileA(activeFilenames[slotIndex]);
                DeleteFileA(finalFile);
                
                // No delay - maximum aggression
            }
        }
        
        if (iteration % 10 == 0) {
            printf("[RACE] Thread %d completed aggressive iteration %d/%d\n", threadId, iteration, ITERATION_COUNT);
        }
        
        // Very short delay between iterations for sustained aggression
        PortableSleep(50);
    }
    
    // Clean up any remaining open files
    for (int i = 0; i < MAX_CONCURRENT_FILES; ++i) {
        if (activeFiles[i] != INVALID_HANDLE_VALUE) {
            CloseHandle(activeFiles[i]);
        }
    }
    
    printf("[RACE] Aggressive thread %d finished\n", threadId);
    DecrementActiveThreads();
    
#ifndef LINUX_DEMO
    return 0;
#else
    return NULL;
#endif
}

// Main stress test that aggressively targets UDFS race condition
void RunKernelCrashTest(const char* testPath) {
    printf("\n===========================================\n");
    printf("UDFS KERNEL CRASH TEST (Enhanced Race Condition Targeting)\n");
    printf("===========================================\n");
    printf("⚠️  WARNING: THIS TEST IS DESIGNED TO CRASH THE KERNEL!\n");
    printf("⚠️  SAVE ALL YOUR WORK BEFORE RUNNING!\n");
    printf("===========================================\n");
    
    printf("Test path: %s\n", testPath);
    IsUDFFilesystem(testPath);
    
    printf("\nEnhanced aggressive approach targeting UDFS race condition:\n");
    printf("- %d aggressive threads (HIGH concurrency for race condition)\n", NUM_WORKER_THREADS);
    printf("- %d mixed files per thread per iteration (small + large)\n", FILES_PER_THREAD);
    printf("- %d iterations per thread (sustained stress)\n", ITERATION_COUNT);
    printf("- Mixed file sizes: 4KB-256KB (small) and 1-100MB (large)\n");
    printf("- %d KB chunks with immediate flush (no buffering)\n", CHUNK_SIZE/1024);
    printf("- Overlapping operations: create/write/read/delete simultaneously\n");
    printf("- Directory operations mixed with file operations\n");
    printf("- Random access patterns mixed with sequential writes\n");
    printf("- Above-normal thread priority for aggressive timing\n");
    printf("- No delays between operations for maximum race condition potential\n");
    printf("- Multiple concurrent file handles per thread\n");
    
    printf("\nPress Enter to start aggressive race condition targeting...\n");
#ifdef LINUX_DEMO
    printf("[LINUX DEMO] In real ReactOS, this would aggressively target the UDFS race condition!\n");
#endif
    getchar();
    
    printf("\n[RACE] Starting aggressive race condition targeting...\n");
    
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
    
    // Start aggressive file stress workers
    for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
        threadDataList[i].threadId = i;
        threadDataList[i].testPath = (char*)testPath;
        
#ifndef LINUX_DEMO
        workerHandles[i] = CreateThread(NULL, 0, FileStressWorkerThread, &threadDataList[i], 0, NULL);
        if (workerHandles[i] != NULL) {
            // Set high priority immediately
            SetThreadPriority(workerHandles[i], THREAD_PRIORITY_ABOVE_NORMAL);
        }
#else
        pthread_create(&workerHandles[i], NULL, FileStressWorkerThread, &threadDataList[i]);
#endif
    }
    
    printf("[RACE] Total aggressive threads launched: %d\n", NUM_WORKER_THREADS);
    printf("[RACE] Each thread will perform overlapping file operations to maximize race condition potential\n");
    
    // Monitor progress with more frequent updates
    clock_t startTime = clock();
    int lastActiveCount = NUM_WORKER_THREADS;
    while (GetActiveThreads() > 0) {
        PortableSleep(5000); // Check every 5 seconds for more responsive monitoring
        clock_t elapsed = (clock() - startTime) / CLOCKS_PER_SEC;
        int currentActive = GetActiveThreads();
        
        printf("[RACE] Active threads: %d, Elapsed: %lds", currentActive, elapsed);
        if (currentActive < lastActiveCount) {
            printf(" (threads completing - crash may be imminent!)");
        }
        printf("\n");
        
        if (elapsed % 30 == 0) { // Every 30 seconds show detailed status
            printf("    STATUS: %d threads aggressively targeting UDFS race condition with overlapping operations...\n", currentActive);
            printf("    PATTERN: Mixed small/large files, concurrent handles, directory ops, random access\n");
        }
        
        lastActiveCount = currentActive;
        
        // Extended timeout but with more warnings (20 minutes)
        if (elapsed > 1200) {
            printf("\n[RACE] Aggressive test running for 20+ minutes without crash.\n");
            printf("The race condition may require:\n");
            printf("1. Specific hardware timing conditions (different CPU/VM)\n");
            printf("2. Specific UDFS driver version vulnerability\n");
            printf("3. Different UDF format version (try UDF 1.50 vs 2.01)\n");
            printf("4. Multiple concurrent instances of this program\n");
            printf("5. Memory pressure from other programs\n");
            printf("6. The vulnerability may already be patched in this driver version\n");
            printf("\nContinuing test for maximum chance of reproducing race condition...\n");
        }
        
        // Much longer timeout (45 minutes) for the aggressive approach
        if (elapsed > 2700) {
            printf("\n[RACE] Extended aggressive testing (45+ minutes) suggests race condition not triggered.\n");
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
    
    printf("\n[RACE] Aggressive race condition testing completed without kernel crash.\n");
    printf("Enhanced approach used:\n");
    printf("- %d aggressive threads with above-normal priority\n", NUM_WORKER_THREADS);
    printf("- Mixed small (%dKB-256KB) and large (1-100MB) files\n", MIN_FILE_SIZE/1024);
    printf("- %d KB chunks with immediate flush (no buffering)\n", CHUNK_SIZE/1024);
    printf("- Overlapping create/write/read/delete operations\n");
    printf("- Directory operations mixed with file operations\n");
    printf("- Random access patterns mixed with sequential operations\n");
    printf("- Multiple concurrent file handles per thread\n");
    printf("- No delays between operations for maximum timing stress\n");
    printf("- Extended runtime for maximum race condition opportunity\n");
}

// Cleanup any leftover Steam download files
void Cleanup(const char* testPath) {
    printf("[STEAM] Cleaning up download files...\n");
    
    WIN32_FIND_DATAA findData;
    char searchPattern[1024];
    
    // Clean up Steam-like file patterns
    const char* patterns[] = {
        "*.downloading", 
        "game_*.pak", 
        "game_*.dll", 
        "game_*.bundle",
        "game_*.archive", 
        "game_*.bank", 
        "game_*.mesh", 
        "game_*.bin", 
        "game_*.cfg",
        "stress_*.tmp", 
        "race_*.tmp"
    };
    
    for (int p = 0; p < 11; ++p) {
        snprintf(searchPattern, sizeof(searchPattern), "%s%s", testPath, patterns[p]);
        HANDLE hFind = FindFirstFileA(searchPattern, &findData);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                char fullPath[1024];
                snprintf(fullPath, sizeof(fullPath), "%s%s", testPath, findData.cFileName);
                printf("[STEAM] Removing: %s\n", fullPath);
                DeleteFileA(fullPath);
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
        }
    }
}

int main(int argc, char* argv[]) {
#ifdef LINUX_DEMO
    printf("UDFS Driver REAL KERNEL CRASH Test (Enhanced Race Condition Targeting - Linux Demo Mode)\n");
#else
    printf("UDFS Driver REAL KERNEL CRASH Test (Enhanced Race Condition Targeting)\n");
#endif
    printf("====================================================================\n");
    printf("This program aggressively targets the UDFS driver race condition that causes\n");
    printf("UNEXPECTED_KERNEL_MODE_TRAP (0x7F) BSOD using overlapping concurrent operations.\n");
    printf("Enhanced approach uses 24 threads with mixed file sizes and no operation delays.\n");
    
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