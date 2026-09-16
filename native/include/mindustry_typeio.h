#ifndef MINDUSTRY_NATIVE_TYPEIO_H
#define MINDUSTRY_NATIVE_TYPEIO_H

#include "mindustry_format.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

/*
 * Advances over one TypeIO.writeObject payload and returns its exact byte
 * span. Values are intentionally retained as raw bytes by the schematic
 * codec; this preserves configs while their semantic content registry is
 * being ported.
 */
MC_API McStatus mc_typeio_skip(McBuffer *input, size_t *bytes);

#endif
