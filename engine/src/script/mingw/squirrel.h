// MinGW-w64 only (engine/CMakeLists.txt puts this folder first): Squirrel 2.2.4 types SQInteger as
// `long` on 64-bit hosts that are not MSVC - 32 bits on Windows, too small for its pointers. Seen
// as MSVC while its header is read, it takes __int64 (MinGW's headers have it), as MSVC builds do.
// (_MSC_VER below 1400: its VS8 wchar_t handling left alone.) The vendored header stays unmodified.
#if defined(_WIN64) && !defined(_MSC_VER)
#define _MSC_VER 1300
#define CR2_SQUIRREL_AS_MSVC
#endif
#include_next <squirrel.h>
#ifdef CR2_SQUIRREL_AS_MSVC
#undef _MSC_VER
#undef CR2_SQUIRREL_AS_MSVC
#endif
