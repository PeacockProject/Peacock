#include <stddef.h>
#include <stdint.h>

#include "mk_zlib_gzip.h"
#include "zlib.h"

#ifdef __aarch64__
extern void uart_puts_all(const char *s);
extern void uart_puthex64_all(uint64_t v);

static void mk_ztrace(const char *s)
{
    uart_puts_all(s);
}

static void mk_ztrace_hex(const char *label, uint64_t value)
{
    uart_puts_all(label);
    uart_puthex64_all(value);
    uart_puts_all("\r\n");
}

static void mk_ztrace_inflate(const char *label, int rc, const z_stream *stream)
{
    uart_puts_all(label);
    uart_puts_all(" rc=0x");
    uart_puthex64_all((uint64_t) (uint32_t) rc);
    uart_puts_all(" in=0x");
    uart_puthex64_all((uint64_t) stream->total_in);
    uart_puts_all(" out=0x");
    uart_puthex64_all((uint64_t) stream->total_out);
    uart_puts_all("\r\n");
}
#else
static void mk_ztrace(const char *s)
{
    (void) s;
}

static void mk_ztrace_hex(const char *label, uint64_t value)
{
    (void) label;
    (void) value;
}

static void mk_ztrace_inflate(const char *label, int rc, const z_stream *stream)
{
    (void) label;
    (void) rc;
    (void) stream;
}
#endif

#define MK_ZLIB_GZIP_HEADER_LEN 10U
#define MK_ZLIB_GZIP_FILENAME_LIMIT 256U
#define MK_ZLIB_WORKSPACE_SIZE (128U * 1024U)
#define MK_ZLIB_ALIGN 16U

typedef struct {
    uint8_t buf[MK_ZLIB_WORKSPACE_SIZE];
    uint32_t off;
} mk_zlib_workspace_t;

static mk_zlib_workspace_t s_mk_zlib_workspace __attribute__((aligned(MK_ZLIB_ALIGN)));

static uint32_t mk_zlib_align_up(uint32_t value)
{
    return (value + (MK_ZLIB_ALIGN - 1U)) & ~(MK_ZLIB_ALIGN - 1U);
}

static void mk_zlib_workspace_reset(void)
{
    s_mk_zlib_workspace.off = 0U;
}

static voidpf mk_zalloc(voidpf opaque, uInt items, uInt size)
{
    uint32_t bytes;
    uint32_t aligned_off;
    void *ptr;

    (void) opaque;

    if (items == 0U || size == 0U) {
        return Z_NULL;
    }
    if ((size_t) items > (SIZE_MAX / (size_t) size)) {
        return Z_NULL;
    }

    bytes = (uint32_t) ((size_t) items * (size_t) size);
    aligned_off = mk_zlib_align_up(s_mk_zlib_workspace.off);
    if (aligned_off > MK_ZLIB_WORKSPACE_SIZE ||
        bytes > (MK_ZLIB_WORKSPACE_SIZE - aligned_off)) {
#ifdef __aarch64__
        uart_puts_all("[mk] zlib: zalloc oom items=0x");
        uart_puthex64_all((uint64_t) items);
        uart_puts_all(" size=0x");
        uart_puthex64_all((uint64_t) size);
        uart_puts_all(" bytes=0x");
        uart_puthex64_all((uint64_t) bytes);
        uart_puts_all(" off=0x");
        uart_puthex64_all((uint64_t) s_mk_zlib_workspace.off);
        uart_puts_all(" align=0x");
        uart_puthex64_all((uint64_t) aligned_off);
        uart_puts_all("\r\n");
#endif
        return Z_NULL;
    }

    ptr = &s_mk_zlib_workspace.buf[aligned_off];
    s_mk_zlib_workspace.off = aligned_off + mk_zlib_align_up(bytes);
#ifdef __aarch64__
    uart_puts_all("[mk] zlib: zalloc items=0x");
    uart_puthex64_all((uint64_t) items);
    uart_puts_all(" size=0x");
    uart_puthex64_all((uint64_t) size);
    uart_puts_all(" bytes=0x");
    uart_puthex64_all((uint64_t) bytes);
    uart_puts_all(" ptr=0x");
    uart_puthex64_all((uint64_t) (uintptr_t) ptr);
    uart_puts_all(" off=0x");
    uart_puthex64_all((uint64_t) s_mk_zlib_workspace.off);
    uart_puts_all("\r\n");
#endif
    return ptr;
}

static void mk_zfree(voidpf opaque, voidpf address)
{
    (void) opaque;
    (void) address;
}

static int mk_zlib_skip_optional_header(const uint8_t *in_buf, uint32_t in_len,
                                        uint32_t *payload_off)
{
    uint32_t pos = MK_ZLIB_GZIP_HEADER_LEN;
    uint8_t flags;
    uint32_t i;

    if (in_len < 18U || in_buf == NULL || payload_off == NULL) {
        return -1;
    }
    if (in_buf[0] != 0x1FU || in_buf[1] != 0x8BU || in_buf[2] != 0x08U) {
        return -1;
    }

    flags = in_buf[3];

    if ((flags & 0x04U) != 0U) {
        uint32_t extra_len;
        if (pos + 2U > in_len) {
            return -1;
        }
        extra_len = (uint32_t) in_buf[pos] |
                    ((uint32_t) in_buf[pos + 1U] << 8);
        pos += 2U;
        if (extra_len > (in_len - pos)) {
            return -1;
        }
        pos += extra_len;
    }

    if ((flags & 0x08U) != 0U) {
        for (i = 0U; i < MK_ZLIB_GZIP_FILENAME_LIMIT; i++) {
            if (pos >= in_len) {
                return -1;
            }
            if (in_buf[pos++] == 0U) {
                break;
            }
        }
        if (i == MK_ZLIB_GZIP_FILENAME_LIMIT) {
            return -1;
        }
    }

    if ((flags & 0x10U) != 0U) {
        while (pos < in_len && in_buf[pos] != 0U) {
            pos++;
        }
        if (pos >= in_len) {
            return -1;
        }
        pos++;
    }

    if ((flags & 0x02U) != 0U) {
        if (pos + 2U > in_len) {
            return -1;
        }
        pos += 2U;
    }

    if (pos >= in_len) {
        return -1;
    }

    *payload_off = pos;
    return 0;
}

int mk_zlib_is_gzip_package(const uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len < MK_ZLIB_GZIP_HEADER_LEN) {
        return 0;
    }
    if (buf[0] != 0x1FU || buf[1] != 0x8BU || buf[2] != 0x08U) {
        return 0;
    }
    return 1;
}

int mk_zlib_decompress_gzip(const uint8_t *in_buf, uint32_t in_len,
                            uint8_t *out_buf, uint32_t out_buf_len,
                            uint32_t *consumed_len, uint32_t *out_len)
{
    z_stream stream;
    uint32_t payload_off;
    int rc;

    mk_ztrace("[mk] zlib: enter\r\n");

    if (out_buf == NULL || out_buf_len == 0U) {
        return -1;
    }
    if (mk_zlib_skip_optional_header(in_buf, in_len, &payload_off) != 0) {
        return -1;
    }
    mk_ztrace_hex("[mk] zlib: hdr ok payload=0x", (uint64_t) payload_off);

    mk_zlib_workspace_reset();
    mk_ztrace("[mk] zlib: workspace reset\r\n");

    stream.next_in = (Bytef *)(uintptr_t)(in_buf + payload_off);
    stream.avail_in = in_len - payload_off;
    stream.next_out = out_buf;
    stream.avail_out = out_buf_len;
    stream.total_in = 0;
    stream.total_out = 0;
    stream.msg = Z_NULL;
    stream.data_type = 0;
    stream.adler = 0;
    stream.reserved = 0;
    stream.state = Z_NULL;
    mk_ztrace("[mk] zlib: stream init done\r\n");

    stream.zalloc = mk_zalloc;
    stream.zfree = mk_zfree;
    stream.opaque = Z_NULL;
    mk_ztrace("[mk] zlib: alloc install done\r\n");

    mk_ztrace("[mk] zlib: inflateInit2 call\r\n");
    rc = inflateInit2(&stream, -MAX_WBITS);
    mk_ztrace_hex("[mk] zlib: after inflateInit2 rc=0x", (uint64_t) (uint32_t) rc);
    if (rc != Z_OK) {
        return -1;
    }

    do {
        mk_ztrace("[mk] zlib: before inflate\r\n");
        rc = inflate(&stream, Z_NO_FLUSH);
        mk_ztrace_inflate("[mk] zlib: after inflate", rc, &stream);
    } while (rc == Z_OK && stream.avail_out != 0U);

    if (rc != Z_STREAM_END) {
        inflateEnd(&stream);
        return -1;
    }

    mk_ztrace("[mk] zlib: trailer begin\r\n");
    if (consumed_len != NULL) {
        *consumed_len = payload_off + (uint32_t) stream.total_in + 8U;
    }
    if (out_len != NULL) {
        *out_len = (uint32_t) stream.total_out;
    }

    inflateEnd(&stream);
    mk_ztrace("[mk] zlib: trailer done\r\n");
    return 0;
}
