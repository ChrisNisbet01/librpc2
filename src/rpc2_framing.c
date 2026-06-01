#include "rpc2_framing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct framing_content_length_st
{
    framing_st base;
    bool in_header;
    int content_length;
    size_t body_offset;
} framing_content_length_st;

static frame_decode_result_t
content_length_decode(framing_st * f, char * buf, size_t buf_len, size_t * msg_offset, size_t * msg_len)
{
    framing_content_length_st * fl = (framing_content_length_st *)f;

    for (;;)
    {
        if (fl->in_header)
        {
            char * content_len_str = strstr(buf, "Content-Length: ");

            if (content_len_str == NULL)
            {
                if (strstr(buf, "\r\n\r\n"))
                {
                    fprintf(stderr, "[FRAME] Error: missing Content-Length header\n");
                    return FRAME_ERROR;
                }
                return FRAME_NEED_MORE;
            }

            if (sscanf(content_len_str, "Content-Length: %d", &fl->content_length) != 1 || fl->content_length < 0)
            {
                fprintf(stderr, "[FRAME] Error: invalid Content-Length\n");
                return FRAME_ERROR;
            }

            char * header_end = strstr(buf, "\r\n\r\n");
            if (header_end == NULL)
            {
                return FRAME_NEED_MORE;
            }

            fl->body_offset = (size_t)(header_end - buf) + 4;
            fl->in_header = false;
            fprintf(
                stderr,
                "[FRAME] Content-Length: %d, body_offset=%zu, buf_len=%zu\n",
                fl->content_length,
                fl->body_offset,
                buf_len
            );
        }

        if (!fl->in_header)
        {
            if (buf_len < fl->body_offset + (size_t)fl->content_length)
            {
                return FRAME_NEED_MORE;
            }

            *msg_offset = fl->body_offset;
            *msg_len = (size_t)fl->content_length;
            fprintf(stderr, "[FRAME] Decoded: msg_offset=%zu, msg_len=%zu\n", *msg_offset, *msg_len);

            fl->body_offset = 0;
            fl->content_length = -1;
            fl->in_header = true;
            return FRAME_DECODED;
        }
    }
}

static char *
content_length_encode(framing_st * f, char const * body, size_t body_len, size_t * framed_len)
{
    (void)f;

    char header[64];
    int hdr_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", body_len);

    *framed_len = (size_t)hdr_len + body_len;
    char * framed = malloc(*framed_len);

    if (framed == NULL)
    {
        *framed_len = 0;
        return NULL;
    }

    memcpy(framed, header, (size_t)hdr_len);
    memcpy(framed + (size_t)hdr_len, body, body_len);
    return framed;
}

static void
content_length_destroy(framing_st * f)
{
    free(f);
}

framing_st *
framing_content_length_create(void)
{
    framing_content_length_st * fl = calloc(1, sizeof(*fl));

    if (fl == NULL)
    {
        return NULL;
    }

    fl->base.decode = content_length_decode;
    fl->base.encode = content_length_encode;
    fl->base.destroy = content_length_destroy;
    fl->in_header = true;
    fl->content_length = -1;
    return &fl->base;
}

typedef struct framing_newline_st
{
    framing_st base;
} framing_newline_st;

static frame_decode_result_t
newline_decode(framing_st * f, char * buf, size_t buf_len, size_t * msg_offset, size_t * msg_len)
{
    (void)f;

    char * nl = memchr(buf, '\n', buf_len);

    if (nl == NULL)
    {
        return FRAME_NEED_MORE;
    }

    *msg_offset = 0;
    *msg_len = (size_t)(nl - buf) + 1;

    return FRAME_DECODED;
}

static char *
newline_encode(framing_st * f, char const * body, size_t body_len, size_t * framed_len)
{
    (void)f;

    *framed_len = body_len + 1;
    char * framed = malloc(*framed_len);

    if (framed == NULL)
    {
        *framed_len = 0;
        return NULL;
    }

    memcpy(framed, body, body_len);
    framed[body_len] = '\n';
    return framed;
}

static void
newline_destroy(framing_st * f)
{
    free(f);
}

framing_st *
framing_newline_create(void)
{
    framing_newline_st * fl = calloc(1, sizeof(*fl));

    if (fl == NULL)
    {
        return NULL;
    }

    fl->base.decode = newline_decode;
    fl->base.encode = newline_encode;
    fl->base.destroy = newline_destroy;
    return &fl->base;
}
