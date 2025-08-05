#!/bin/bash
# Test script to verify udfs_crash_repro.cpp compilation across different environments

echo "Testing udfs_crash_repro.cpp compilation..."
echo "============================================="

# Test 1: Linux demo with C++11
echo "Test 1: Linux demo with C++11"
if g++ -std=c++11 -o udfs_crash_repro udfs_crash_repro.cpp -pthread -DLINUX_DEMO 2>/dev/null; then
    echo "✓ PASS: Linux demo C++11 compilation"
    rm -f udfs_crash_repro
else
    echo "✗ FAIL: Linux demo C++11 compilation"
fi

# Test 2: MinGW 64-bit with C++11  
echo "Test 2: MinGW 64-bit with C++11"
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    if x86_64-w64-mingw32-g++ -std=c++11 -o udfs_crash_repro.exe udfs_crash_repro.cpp -pthread 2>/dev/null; then
        echo "✓ PASS: MinGW 64-bit C++11 compilation"
        rm -f udfs_crash_repro.exe
    else
        echo "✗ FAIL: MinGW 64-bit C++11 compilation"
    fi
else
    echo "- SKIP: MinGW 64-bit not available"
fi

# Test 3: MinGW 32-bit with C++11
echo "Test 3: MinGW 32-bit with C++11"
if command -v i686-w64-mingw32-g++ >/dev/null 2>&1; then
    if i686-w64-mingw32-g++ -std=c++11 -o udfs_crash_repro32.exe udfs_crash_repro.cpp -pthread 2>/dev/null; then
        echo "✓ PASS: MinGW 32-bit C++11 compilation"
        rm -f udfs_crash_repro32.exe
    else
        echo "✗ FAIL: MinGW 32-bit C++11 compilation"
    fi
else
    echo "- SKIP: MinGW 32-bit not available"
fi

# Test 4: Verify C++98 fails with clear error
echo "Test 4: C++98 should fail with clear error message"
if g++ -std=c++98 -o udfs_crash_repro98 udfs_crash_repro.cpp -pthread -DLINUX_DEMO 2>&1 | grep -q "This code requires C++11 or later"; then
    echo "✓ PASS: C++98 fails with clear error message"
else
    echo "✗ FAIL: C++98 should fail with clear error message"
fi

# Test 5: C++17 compatibility
echo "Test 5: C++17 compatibility"
if g++ -std=c++17 -o udfs_crash_repro17 udfs_crash_repro.cpp -pthread -DLINUX_DEMO 2>/dev/null; then
    echo "✓ PASS: C++17 compilation"
    rm -f udfs_crash_repro17
else
    echo "✗ FAIL: C++17 compilation"
fi

echo "============================================="
echo "Compilation tests completed"