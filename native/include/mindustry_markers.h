#ifndef MINDUSTRY_NATIVE_MARKERS_H
#define MINDUSTRY_NATIVE_MARKERS_H

#include "mindustry_format.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

typedef enum McUbjsonKind{
    MC_UBJSON_NULL = 0,
    MC_UBJSON_TRUE,
    MC_UBJSON_FALSE,
    MC_UBJSON_UINT8,
    MC_UBJSON_UINT16,
    MC_UBJSON_INT8,
    MC_UBJSON_INT16,
    MC_UBJSON_INT32,
    MC_UBJSON_INT64,
    MC_UBJSON_FLOAT32,
    MC_UBJSON_FLOAT64,
    MC_UBJSON_CHAR,
    MC_UBJSON_STRING,
    MC_UBJSON_ARRAY,
    MC_UBJSON_OBJECT
} McUbjsonKind;

typedef struct McUbjsonValue{
    McUbjsonKind kind;
    uint64_t integer;
    double real;
    char *string;
    struct McUbjsonValue **values;
    char **keys;
    size_t count;
} McUbjsonValue;

typedef struct McMarkers{
    McUbjsonValue root;
} McMarkers;

/* MapMarkers is a UBJSON object. The DOM preserves marker fields and all
   primitive widths; optimized UBJSON arrays are expanded semantically. */
MC_API McStatus mc_markers_read(const uint8_t *data, size_t size, McMarkers *markers);
MC_API McStatus mc_markers_write(const McMarkers *markers, McBuffer *output);
MC_API void mc_markers_destroy(McMarkers *markers);
MC_API void mc_markers_init_empty(McMarkers *markers);

#endif
