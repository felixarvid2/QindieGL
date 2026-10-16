// Force-included for the MinGW/clang build: pulls in things MSVC's headers leak implicitly.
#pragma once
#include <float.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef __cplusplus
#include <algorithm>
using std::min;
using std::max;
#endif
