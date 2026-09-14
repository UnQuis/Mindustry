#ifndef MINDUSTRY_NATIVE_SAVE_H
#define MINDUSTRY_NATIVE_SAVE_H

#include "mindustry_format.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

#define MC_SAVE_HEADER_0 ((uint8_t)'M')
#define MC_SAVE_HEADER_1 ((uint8_t)'S')
#define MC_SAVE_HEADER_2 ((uint8_t)'A')
#define MC_SAVE_HEADER_3 ((uint8_t)'V')

typedef struct McSaveTag{
    const char *key;
    const char *value;
} McSaveTag;

typedef struct McSaveTags{
    char **keys;
    char **values;
    size_t count;
} McSaveTags;

/* ContentType ordinals are serialized IDs; the Java enum explicitly forbids rearranging them. */
enum{
    MC_CONTENT_ITEM = 0,
    MC_CONTENT_BLOCK = 1,
    MC_CONTENT_BULLET = 3,
    MC_CONTENT_LIQUID = 4,
    MC_CONTENT_STATUS = 5,
    MC_CONTENT_UNIT = 6,
    MC_CONTENT_WEATHER = 7,
    MC_CONTENT_SECTOR = 9,
    MC_CONTENT_PLANET = 13,
    MC_CONTENT_TEAM = 15,
    MC_CONTENT_UNIT_COMMAND = 16,
    MC_CONTENT_UNIT_STANCE = 17,
    MC_CONTENT_TYPE_COUNT = 18
};

typedef struct McContentGroupView{
    uint8_t type;
    const char *const *names;
    size_t count;
} McContentGroupView;

typedef struct McContentGroup{
    uint8_t type;
    char **names;
    size_t count;
} McContentGroup;

typedef struct McContentHeader{
    McContentGroup *groups;
    size_t count;
} McContentHeader;

typedef struct McPlainMapSave{
    uint32_t version;
    McSaveTags meta;
    McContentHeader content;
    McWorld world;
} McPlainMapSave;

/* Java SaveIO header and SaveFileReader length-prefixed regions. */
MC_API McStatus mc_save_write_header(McBuffer *output, uint32_t version);
MC_API McStatus mc_save_read_header(McBuffer *input, uint32_t *version);
MC_API McStatus mc_save_write_region(McBuffer *output, const uint8_t *data, size_t size);
MC_API McStatus mc_save_read_region(McBuffer *input, const uint8_t **data, size_t *size);

/* Java DataOutput.writeUTF/readUTF-compatible modified UTF-8 strings. */
MC_API McStatus mc_save_write_utf(McBuffer *output, const char *utf8);
MC_API McStatus mc_save_read_utf(McBuffer *input, char **utf8);

/* Java SaveFileReader.writeStringMap/readStringMap-compatible tag maps. */
MC_API McStatus mc_save_write_string_map(McBuffer *output, const McSaveTag *tags, size_t count);
MC_API McStatus mc_save_read_string_map(McBuffer *input, McSaveTags *tags);
MC_API void mc_save_tags_destroy(McSaveTags *tags);

/* Java SaveVersion.writeContentHeader/readContentHeader-compatible mapping. */
MC_API McStatus mc_save_write_content_header(McBuffer *output, const McContentGroupView *groups, size_t count);
MC_API McStatus mc_save_read_content_header(McBuffer *input, McContentHeader *header);
MC_API int32_t mc_content_header_find(const McContentHeader *header, uint8_t type, const char *name);
MC_API void mc_content_header_destroy(McContentHeader *header);

/* Reads a current (version 8..13) zlib-wrapped save when its map section has
   no building/entity/custom tile records. Other regions are validated and
   skipped, never silently interpreted as plain tiles. */
MC_API McStatus mc_save_read_plain_map(const uint8_t *compressed, size_t size, McPlainMapSave *save);
MC_API void mc_plain_map_save_destroy(McPlainMapSave *save);

#endif
