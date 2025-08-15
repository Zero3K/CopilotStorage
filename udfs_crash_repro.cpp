/*
 * UDFS Driver REAL KERNEL CRASH Test Program (Steam Download Simulator)
 * 
 * WARNING: THIS PROGRAM IS DESIGNED TO CRASH THE KERNEL!
 * Only run this if you want to trigger the actual UDFS driver BSOD for debugging purposes.
 * 
 * This program triggers the real race condition in the ReactOS UDFS driver that causes:
 * UNEXPECTED_KERNEL_MODE_TRAP (0x7F) - when RemoveHeadList() is called on corrupted list
 * 
 * ENHANCED: Now simulates Steam's download behavior to trigger crashes more effectively:
 * - Large file downloads (1MB-500MB) similar to game files
 * - Chunked streaming writes like Steam's download engine
 * - Steam-like file naming (.downloading, .tmp, renaming patterns)
 * - Concurrent multi-file downloads simulation
 * - Progress tracking and validation operations
 * - High sustained I/O patterns that match Steam's behavior
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

// Strategic test parameters designed to simulate Steam download behavior
static const int NUM_WORKER_THREADS = 8;          // Moderate thread count like Steam's concurrent downloads
static const int FILES_PER_THREAD = 5;            // Fewer, larger files like Steam game downloads  
static const int MAX_FILE_SIZE = 500 * 1024 * 1024; // Large files up to 500MB (like game files)
static const int MIN_FILE_SIZE = 1 * 1024 * 1024;   // Minimum 1MB files (realistic game file sizes)
static const int ITERATION_COUNT = 50;             // Fewer iterations with larger files
static const int CHUNK_SIZE = 64 * 1024;           // 64KB chunks like Steam's download chunks
static const int STEAM_PROGRESS_INTERVAL = 10;     // Progress updates every 10 chunks
static const int CONCURRENT_DOWNLOADS = 3;         // Simulate multiple concurrent game downloads

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

// Simulate Steam's chunked download writing
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
        
        // Force immediate write (Steam flushes frequently)
        FlushFileBuffers(hFile);
        
        downloaded += bytesWritten;
        chunkCount++;
        
        // Show progress like Steam (every N chunks)
        if (chunkCount % STEAM_PROGRESS_INTERVAL == 0) {
            ShowDownloadProgress(filename, downloaded, totalSize);
        }
        
        free(chunkData);
        
        // Small delay to simulate network download speed
        PortableMicroSleep(500);
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

// Steam download simulation worker thread function
#ifndef LINUX_DEMO
DWORD WINAPI FileStressWorkerThread(LPVOID param) {
#else
void* FileStressWorkerThread(void* param) {
#endif
    struct ThreadData* data = (struct ThreadData*)param;
    int threadId = data->threadId;
    char* testPath = data->testPath;
    
    IncrementActiveThreads();
    
    // Set thread to normal priority (Steam uses normal priority for downloads)
#ifndef LINUX_DEMO
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#else
    printf("[MOCK] SetThreadPriority(THREAD_PRIORITY_NORMAL)\n");
#endif
    
    printf("[STEAM] Download thread %d started (simulating Steam game downloads)\n", threadId);
    
    for (int iteration = 0; iteration < ITERATION_COUNT && !GetShouldStop(); ++iteration) {
        // Simulate Steam downloading multiple game files concurrently
        for (int fileIndex = 0; fileIndex < FILES_PER_THREAD && !GetShouldStop(); ++fileIndex) {
            
            // Phase 1: Create .downloading file (Steam's temp download file)
            char downloadingFile[1024];
            char finalFile[1024];
            GenerateSteamFileName(threadId, fileIndex, finalFile, sizeof(finalFile));
            snprintf(downloadingFile, sizeof(downloadingFile), "%s%s.downloading", testPath, finalFile);
            snprintf(finalFile, sizeof(finalFile), "%s%s", testPath, finalFile);
            
            size_t fileSize = RandomInRange(MIN_FILE_SIZE, MAX_FILE_SIZE);
            printf("[STEAM] Thread %d starting download: %s (%zu bytes)\n", 
                   threadId, downloadingFile, fileSize);
            
            // Create downloading file with Steam-like flags
            HANDLE hDownloadFile = CreateFileA(
                downloadingFile,
                GENERIC_WRITE | GENERIC_READ,
                FILE_SHARE_READ,  // Allow reading while downloading
                NULL,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                NULL
            );
            
            if (hDownloadFile == INVALID_HANDLE_VALUE) {
                printf("[STEAM] Failed to create download file: %s\n", downloadingFile);
                continue;
            }
            
            // Phase 2: Simulate chunked download (like Steam's streaming download)
            if (SteamChunkedDownload(hDownloadFile, downloadingFile, fileSize)) {
                printf("[STEAM] Download completed: %s\n", downloadingFile);
                
                // Phase 3: Validate downloaded file (like Steam's integrity check)
                ValidateDownloadedFile(hDownloadFile, downloadingFile);
                
                CloseHandle(hDownloadFile);
                
                // Phase 4: Rename to final file (Steam's atomic completion)
                printf("[STEAM] Installing: %s -> %s\n", downloadingFile, finalFile);
                
                // Steam's pattern: rename temp file to final name
                DeleteFileA(finalFile); // Remove if exists
                
                // Simulate rename by creating final file and copying content
                HANDLE hFinalFile = CreateFileA(
                    finalFile,
                    GENERIC_WRITE | GENERIC_READ,
                    FILE_SHARE_READ,
                    NULL,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                
                if (hFinalFile != INVALID_HANDLE_VALUE) {
                    // Copy content from downloading file to final file
                    HANDLE hSourceFile = CreateFileA(
                        downloadingFile,
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL
                    );
                    
                    if (hSourceFile != INVALID_HANDLE_VALUE) {
                        char copyBuffer[CHUNK_SIZE];
                        DWORD bytesRead, bytesWritten;
                        
                        while (ReadFile(hSourceFile, copyBuffer, sizeof(copyBuffer), &bytesRead, NULL) && bytesRead > 0) {
                            WriteFile(hFinalFile, copyBuffer, bytesRead, &bytesWritten, NULL);
                            FlushFileBuffers(hFinalFile);
                        }
                        
                        CloseHandle(hSourceFile);
                    }
                    
                    CloseHandle(hFinalFile);
                }
                
                // Delete the .downloading file (Steam cleanup)
                DeleteFileA(downloadingFile);
                
                printf("[STEAM] Installation complete: %s\n", finalFile);
                
            } else {
                CloseHandle(hDownloadFile);
                DeleteFileA(downloadingFile);
                printf("[STEAM] Download failed: %s\n", downloadingFile);
            }
            
            // Simulate concurrent downloads with small delay
            PortableSleep(100);
        }
        
        // Phase 5: Simulate Steam's post-download verification
        for (int fileIndex = 0; fileIndex < FILES_PER_THREAD && !GetShouldStop(); ++fileIndex) {
            char finalFile[1024];
            GenerateSteamFileName(threadId, fileIndex, finalFile, sizeof(finalFile));
            snprintf(finalFile, sizeof(finalFile), "%s%s", testPath, finalFile);
            
            // Open and validate final file
            HANDLE hFile = CreateFileA(
                finalFile,
                GENERIC_READ,
                FILE_SHARE_READ,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            
            if (hFile != INVALID_HANDLE_VALUE) {
                ValidateDownloadedFile(hFile, finalFile);
                CloseHandle(hFile);
            }
        }
        
        // Phase 6: Cleanup (simulate Steam removing old files)
        for (int fileIndex = 0; fileIndex < FILES_PER_THREAD && !GetShouldStop(); ++fileIndex) {
            char finalFile[1024];
            GenerateSteamFileName(threadId, fileIndex, finalFile, sizeof(finalFile));
            snprintf(finalFile, sizeof(finalFile), "%s%s", testPath, finalFile);
            
            printf("[STEAM] Cleaning up: %s\n", finalFile);
            DeleteFileA(finalFile);
            
            // Small delay to simulate Steam's cleanup process
            PortableSleep(50);
        }
        
        if (iteration % 5 == 0) {
            printf("[STEAM] Thread %d completed iteration %d/%d\n", threadId, iteration, ITERATION_COUNT);
        }
        
        // Steam-like delay between download sessions
        PortableSleep(1000);
    }
    
    printf("[STEAM] Download thread %d finished\n", threadId);
    DecrementActiveThreads();
    
#ifndef LINUX_DEMO
    return 0;
#else
    return NULL;
#endif
}

// Main stress test that simulates Steam downloading games to trigger UDFS race condition
void RunKernelCrashTest(const char* testPath) {
    printf("\n===========================================\n");
    printf("UDFS KERNEL CRASH TEST (Steam Download Simulator)\n");
    printf("===========================================\n");
    printf("⚠️  WARNING: THIS TEST IS DESIGNED TO CRASH THE KERNEL!\n");
    printf("⚠️  SAVE ALL YOUR WORK BEFORE RUNNING!\n");
    printf("===========================================\n");
    
    printf("Test path: %s\n", testPath);
    IsUDFFilesystem(testPath);
    
    printf("\nSimulating Steam download behavior with:\n");
    printf("- %d download threads (like Steam's concurrent downloads)\n", NUM_WORKER_THREADS);
    printf("- %d game files per thread per iteration\n", FILES_PER_THREAD);
    printf("- %d download iterations per thread\n", ITERATION_COUNT);
    printf("- File sizes: %d MB - %d MB (like game files)\n", MIN_FILE_SIZE/(1024*1024), MAX_FILE_SIZE/(1024*1024));
    printf("- %d KB download chunks (like Steam's streaming)\n", CHUNK_SIZE/1024);
    printf("- Steam-like operations: .downloading files, validation, atomic renames\n");
    printf("- High sustained I/O patterns matching Steam's download engine\n");
    
    printf("\nPress Enter to start Steam download simulation to trigger the race condition...\n");
#ifdef LINUX_DEMO
    printf("[LINUX DEMO] In real ReactOS, this would simulate Steam downloads and attempt to trigger kernel crash!\n");
#endif
    getchar();
    
    printf("\n[STEAM] Starting download simulation...\n");
    
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
    
    printf("[STEAM] Total download threads launched: %d\n", NUM_WORKER_THREADS);
    
    // Monitor progress like Steam's download manager
    clock_t startTime = clock();
    while (GetActiveThreads() > 0) {
        PortableSleep(10000); // Check every 10 seconds (like Steam's UI updates)
        clock_t elapsed = (clock() - startTime) / CLOCKS_PER_SEC;
        printf("[STEAM] Active downloads: %d, Elapsed: %lds\n", GetActiveThreads(), elapsed);
        printf("    STATUS: Simulating Steam download engine with %d concurrent threads targeting UDFS race condition...\n", NUM_WORKER_THREADS);
        
        // Extended timeout for large file downloads (30 minutes)
        if (elapsed > 1800) {
            printf("\n[STEAM] Download simulation running for 30+ minutes without crash. This suggests:\n");
            printf("1. Race condition may require very specific timing/hardware conditions\n");
            printf("2. Driver may be fixed or environment doesn't reproduce the issue\n");
            printf("3. UDFS may be handling Steam-like I/O patterns without race condition\n");
            printf("4. Consider running on different hardware/VM configuration\n");
            printf("5. Try running multiple instances simultaneously for increased Steam-like load\n");
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
    
    printf("\n[STEAM] Download simulation completed without kernel crash.\n");
    printf("This Steam download simulation used realistic game download patterns:\n");
    printf("- %d download threads (normal priority, like Steam)\n", NUM_WORKER_THREADS);
    printf("- %d large files per iteration (%d MB - %d MB each)\n", FILES_PER_THREAD, MIN_FILE_SIZE/(1024*1024), MAX_FILE_SIZE/(1024*1024));
    printf("- %d KB streaming chunks with progress tracking\n", CHUNK_SIZE/1024);
    printf("- Steam-like file patterns: .downloading -> final rename\n");
    printf("- File validation and integrity checking simulation\n");
    printf("- Concurrent multi-file downloads with sustained high I/O\n");
    printf("- Atomic file operations and cleanup patterns\n");
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
    printf("UDFS Driver REAL KERNEL CRASH Test (Steam Download Simulator - Linux Demo Mode)\n");
#else
    printf("UDFS Driver REAL KERNEL CRASH Test (Steam Download Simulator)\n");
#endif
    printf("====================================================================\n");
    printf("This program simulates Steam's download behavior to trigger the actual UDFS\n");
    printf("driver race condition that causes UNEXPECTED_KERNEL_MODE_TRAP (0x7F) BSOD.\n");
    printf("Steam's large file downloads with chunked writes may trigger crashes more effectively.\n");
    
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