#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum
{
    FRAME_DECODED,
    FRAME_NEED_MORE,
    FRAME_ERROR,
} frame_decode_result_t;

typedef struct framing_st framing_st;

struct framing_st
{
    frame_decode_result_t (*decode)(framing_st * f, char * buf, size_t buf_len, size_t * msg_offset, size_t * msg_len);
    char * (*encode)(framing_st * f, char const * body, size_t body_len, size_t * framed_len);
    void (*destroy)(framing_st * f);
};

framing_st * framing_content_length_create(void);
framing_st * framing_newline_create(void);
