# Cross-compile QindieGL (32-bit Windows DLL) on Linux with llvm-mingw.
# Usage: cmake -S . -B build-linux -DCMAKE_TOOLCHAIN_FILE=linux/toolchain-i686-llvm-mingw.cmake
set(LLVM_MINGW_ROOT "$ENV{HOME}/.local/llvm-mingw" CACHE PATH "llvm-mingw install prefix")
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_COMPILER   ${LLVM_MINGW_ROOT}/bin/i686-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER ${LLVM_MINGW_ROOT}/bin/i686-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER  ${LLVM_MINGW_ROOT}/bin/i686-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH ${LLVM_MINGW_ROOT}/i686-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
