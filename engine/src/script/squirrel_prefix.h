// Force-included before each Squirrel 2.2.4 source (engine/CMakeLists.txt): its allocator, which
// squtils.h's templates call before sqmem's declarations - accepted by 2009's compilers, not by
// today's two-phase lookup. Declaring it first leaves the vendored source unmodified.
#pragma once
#include "squirrel.h"
void* sq_vm_malloc(SQUnsignedInteger size);
void* sq_vm_realloc(void* p, SQUnsignedInteger oldsize, SQUnsignedInteger size);
void sq_vm_free(void* p, SQUnsignedInteger size);
