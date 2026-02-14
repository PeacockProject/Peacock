#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <linux/kd.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include "peacock_logo.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static int fb_fd = -1;
static int tty_fd = -1;
static char *fb_mem = NULL;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static long int screensize = 0;
static int bytes_per_pixel = 0;

#define LK2ND_FONT_WIDTH 6u
#define LK2ND_FONT_HEIGHT 12u
#define LK2ND_MIN_LINE 40u

static unsigned int parse_rgb_hex(const char *s, unsigned int fallback) {
    if (!s || !*s) return fallback;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (end == s || (end && *end != '\0') || v > 0xFFFFFFUL) return fallback;
    return (unsigned int)v;
}

static int open_active_tty(void) {
    const char *tty_path = "/dev/tty";
    char active[32] = {0};
    int fd = open("/sys/devices/virtual/tty/tty0/active", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, active, sizeof(active) - 1);
        close(fd);
        if (n > 0) {
            while (n > 0 && (active[n - 1] == '\n' || active[n - 1] == '\r')) {
                active[n - 1] = '\0';
                n--;
            }
            if (active[0] == '/') {
                tty_path = active;
            } else if (active[0] != '\0') {
                static char buf[40];
                snprintf(buf, sizeof(buf), "/dev/%s", active);
                tty_path = buf;
            }
        }
    }
    tty_fd = open(tty_path, O_RDWR);
    if (tty_fd < 0) {
        fprintf(stderr, "peacock-splash: failed to open tty %s: %s\n", tty_path, strerror(errno));
        return -1;
    }
    if (ioctl(tty_fd, KDSETMODE, KD_GRAPHICS) != 0) {
        fprintf(stderr, "peacock-splash: KDSETMODE KD_GRAPHICS failed: %s\n", strerror(errno));
    } else {
        fprintf(stderr, "peacock-splash: tty %s set to KD_GRAPHICS\n", tty_path);
    }
    return 0;
}

static void splash_flush(void) {
    vinfo.activate |= FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
    if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo) < 0) {
        ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
    }
}

static unsigned int pack_pixel(unsigned int rgb) {
    unsigned int r = (rgb >> 16) & 0xFF;
    unsigned int g = (rgb >> 8) & 0xFF;
    unsigned int b = rgb & 0xFF;

    unsigned int val = 0;
    if (vinfo.red.length) {
        unsigned int rbits = r >> (8 - vinfo.red.length);
        val |= rbits << vinfo.red.offset;
    }
    if (vinfo.green.length) {
        unsigned int gbits = g >> (8 - vinfo.green.length);
        val |= gbits << vinfo.green.offset;
    }
    if (vinfo.blue.length) {
        unsigned int bbits = b >> (8 - vinfo.blue.length);
        val |= bbits << vinfo.blue.offset;
    }
    if (vinfo.transp.length) {
        unsigned int tbits = (1u << vinfo.transp.length) - 1u;
        val |= tbits << vinfo.transp.offset;
    }
    return val;
}

static inline void draw_pixel(int x, int y, unsigned int color) {
    if (x < 0 || y < 0 || x >= (int)vinfo.xres || y >= (int)vinfo.yres) return;
    if (bytes_per_pixel == 2) {
        unsigned short *row = (unsigned short*)((char*)fb_mem + y * finfo.line_length);
        row[x] = (unsigned short)color;
    } else if (bytes_per_pixel == 3) {
        unsigned char *row = (unsigned char*)fb_mem + y * finfo.line_length;
        int pos = x * 3;
        row[pos + 0] = (unsigned char)(color & 0xFF);
        row[pos + 1] = (unsigned char)((color >> 8) & 0xFF);
        row[pos + 2] = (unsigned char)((color >> 16) & 0xFF);
    } else {
        unsigned int *row = (unsigned int*)((char*)fb_mem + y * finfo.line_length);
        row[x] = color;
    }
}

static void draw_centered_logo(void) {
    if (!fb_mem) return;

    const unsigned int src_w = PEACOCK_LOGO_WIDTH;
    const unsigned int src_h = PEACOCK_LOGO_HEIGHT;
    unsigned int draw_w = (unsigned int)vinfo.xres;
    unsigned int draw_h;
    unsigned int dst_x_off, dst_y_off;
    unsigned int white = pack_pixel(0xFFFFFF);

    if (!draw_w || !vinfo.yres) return;

    /*
     * Match lk2nd sizing exactly:
     * Fit full source image inside framebuffer, preserving aspect ratio.
     */
    draw_h = (unsigned int)(((uint64_t)src_h * draw_w) / src_w);
    if (draw_h > (unsigned int)vinfo.yres) {
        draw_h = (unsigned int)vinfo.yres;
        draw_w = (unsigned int)(((uint64_t)src_w * draw_h) / src_h);
    }

    if (!draw_w || !draw_h) return;

    dst_x_off = ((unsigned int)vinfo.xres - draw_w) / 2u;
    dst_y_off = ((unsigned int)vinfo.yres - draw_h) / 2u;

    for (unsigned int y = 0; y < draw_h; ++y) {
        unsigned int src_y = (unsigned int)(((uint64_t)y * src_h) / draw_h);
        uint32_t row_start = peacock_logo_row_offsets[src_y];
        uint32_t row_end = peacock_logo_row_offsets[src_y + 1];
        int dst_y = (int)(dst_y_off + y);

        for (uint32_t r = row_start; r < row_end; ++r) {
            uint32_t run_x = peacock_logo_runs[r].x;
            uint32_t run_len = peacock_logo_runs[r].len;
            uint32_t run_end = run_x + run_len;
            uint32_t clip_start = (uint32_t)(((uint64_t)run_x * draw_w) / src_w);
            uint32_t clip_end = (uint32_t)((((uint64_t)run_end * draw_w) + src_w - 1u) / src_w);

            if (clip_start >= clip_end) continue;
            if (clip_end > draw_w) clip_end = draw_w;

            for (uint32_t x = clip_start; x < clip_end; ++x) {
                draw_pixel((int)(dst_x_off + x), dst_y, white);
            }
        }
    }
}

static void clear_lk2nd_footer_strip(void) {
    if (!fb_mem) return;

    unsigned int min_dim = (unsigned int)vinfo.xres;
    if ((unsigned int)vinfo.yres < min_dim) min_dim = (unsigned int)vinfo.yres;

    unsigned int scale = min_dim / (LK2ND_FONT_WIDTH * LK2ND_MIN_LINE);
    if (scale < 1u) scale = 1u;

    unsigned int incr = LK2ND_FONT_HEIGHT * scale;
    unsigned int y = ((unsigned int)vinfo.yres > 3u * incr)
        ? ((unsigned int)vinfo.yres - 3u * incr)
        : 0u;

    /* Match lk2nd: start line is integer-divided by FONT_HEIGHT. */
    unsigned int y_start_line = y / LK2ND_FONT_HEIGHT;
    unsigned int start_row = y_start_line * LK2ND_FONT_HEIGHT;
    unsigned int clear_rows = LK2ND_FONT_HEIGHT * 3u * scale;

    if (start_row >= (unsigned int)vinfo.yres) return;
    if (start_row + clear_rows > (unsigned int)vinfo.yres) {
        clear_rows = (unsigned int)vinfo.yres - start_row;
    }

    /*
     * Match lk2nd fbcon clear behavior as closely as possible:
     * write zeroed scanlines directly instead of color packing.
     */
    for (unsigned int row = 0; row < clear_rows; ++row) {
        unsigned int yy = start_row + row;
        memset(fb_mem + (yy * finfo.line_length), 0, finfo.line_length);
    }
}

static unsigned int next_rand_u32(unsigned int *state) {
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void sleep_us(unsigned int us) {
    struct timespec ts;
    ts.tv_sec = (time_t)(us / 1000000u);
    ts.tv_nsec = (long)((us % 1000000u) * 1000u);
    nanosleep(&ts, NULL);
}

static void shift_row_wrap(unsigned char *dst, const unsigned char *src, int line_len, int shift_bytes) {
    if (!dst || !src || line_len <= 0) return;
    if (shift_bytes < 0) {
        shift_bytes = line_len - ((-shift_bytes) % line_len);
    } else {
        shift_bytes %= line_len;
    }
    if (shift_bytes == 0) {
        memcpy(dst, src, (size_t)line_len);
        return;
    }
    int tail = line_len - shift_bytes;
    memcpy(dst, src + shift_bytes, (size_t)tail);
    memcpy(dst + tail, src, (size_t)shift_bytes);
}

static void splash_glitch_brief(void) {
    if (!fb_mem) return;

    unsigned int seed = (unsigned int)(getpid() * 2654435761u) ^ (unsigned int)time(NULL);
    if (!seed) seed = 0x9e3779b9u;

    if (finfo.line_length <= 0 || vinfo.yres <= 0 || vinfo.xres <= 0) return;

    unsigned char *frame_tmp = (unsigned char*)malloc((size_t)screensize);
    unsigned char *line_tmp = (unsigned char*)malloc((size_t)finfo.line_length);
    if (!frame_tmp || !line_tmp) {
        free(frame_tmp);
        free(line_tmp);
        return;
    }

    unsigned int c_hot_white = pack_pixel(0xF4F7FF);
    unsigned int c_magenta   = pack_pixel(0xF045FF);
    unsigned int c_purple    = pack_pixel(0x8A49FF);
    unsigned int c_cyan      = pack_pixel(0x36E7FF);
    unsigned int c_red       = pack_pixel(0xFF4A6E);
    unsigned int palette[] = {c_hot_white, c_magenta, c_purple, c_cyan, c_red};

    for (int pass = 0; pass < 3; ++pass) {
        memcpy(frame_tmp, fb_mem, (size_t)screensize);

        int y = 0;
        while (y < (int)vinfo.yres) {
            int bh = 1 + (int)(next_rand_u32(&seed) % 10u);
            int shift_px = (int)(next_rand_u32(&seed) % 53u) - 26;
            if ((next_rand_u32(&seed) & 7u) == 0u) {
                shift_px = (int)(next_rand_u32(&seed) % 181u) - 90;
            }
            int shift_bytes = shift_px * bytes_per_pixel;
            for (int by = 0; by < bh && y + by < (int)vinfo.yres; ++by) {
                unsigned char *dst = (unsigned char*)fb_mem + ((y + by) * finfo.line_length);
                const unsigned char *src = frame_tmp + ((y + by) * finfo.line_length);
                shift_row_wrap(line_tmp, src, (int)finfo.line_length, shift_bytes);
                memcpy(dst, line_tmp, (size_t)finfo.line_length);
            }
            y += bh;
        }

        int streak_rows = (int)(vinfo.yres / 3u);
        for (int i = 0; i < streak_rows; ++i) {
            int yy = (int)(next_rand_u32(&seed) % vinfo.yres);
            int x0 = (int)(next_rand_u32(&seed) % vinfo.xres);
            int len = 8 + (int)(next_rand_u32(&seed) % 100u);
            unsigned int col = palette[next_rand_u32(&seed) % (sizeof(palette) / sizeof(palette[0]))];
            for (int x = 0; x < len; ++x) {
                int xx = x0 + x;
                if (xx >= (int)vinfo.xres) break;
                draw_pixel(xx, yy, col);
                if ((next_rand_u32(&seed) & 1u) && yy + 1 < (int)vinfo.yres) draw_pixel(xx, yy + 1, col);
            }
        }

        int grain = (int)((vinfo.xres * vinfo.yres) / 420u);
        for (int i = 0; i < grain; ++i) {
            int x = (int)(next_rand_u32(&seed) % vinfo.xres);
            int y2 = (int)(next_rand_u32(&seed) % vinfo.yres);
            unsigned int col = palette[next_rand_u32(&seed) % (sizeof(palette) / sizeof(palette[0]))];
            draw_pixel(x, y2, col);
        }

        splash_flush();
        sleep_us(12000);
    }

    free(frame_tmp);
    free(line_tmp);
}

static void draw_rgba_image_centered(const unsigned char *rgba, int img_w, int img_h) {
    if (!fb_mem || !rgba || img_w <= 0 || img_h <= 0) return;

    unsigned int draw_w = (unsigned int)vinfo.xres;
    unsigned int draw_h = (unsigned int)(((uint64_t)img_h * draw_w) / (uint64_t)img_w);
    if (draw_h > (unsigned int)vinfo.yres) {
        draw_h = (unsigned int)vinfo.yres;
        draw_w = (unsigned int)(((uint64_t)img_w * draw_h) / (uint64_t)img_h);
    }
    if (!draw_w || !draw_h) return;

    unsigned int ox = ((unsigned int)vinfo.xres - draw_w) / 2u;
    unsigned int oy = ((unsigned int)vinfo.yres - draw_h) / 2u;

    for (unsigned int y = 0; y < draw_h; ++y) {
        unsigned int src_y = (unsigned int)(((uint64_t)y * (uint64_t)img_h) / (uint64_t)draw_h);
        for (unsigned int x = 0; x < draw_w; ++x) {
            unsigned int src_x = (unsigned int)(((uint64_t)x * (uint64_t)img_w) / (uint64_t)draw_w);
            const unsigned char *p = rgba + (((size_t)src_y * (size_t)img_w + (size_t)src_x) * 4u);
            if (p[3] < 8) continue;
            unsigned int rgb = ((unsigned int)p[0] << 16) | ((unsigned int)p[1] << 8) | (unsigned int)p[2];
            draw_pixel((int)(ox + x), (int)(oy + y), pack_pixel(rgb));
        }
    }
}

static int draw_png_centered(const char *path) {
    if (!path || !*path) return -1;
    int w = 0, h = 0, ch = 0;
    unsigned char *rgba = stbi_load(path, &w, &h, &ch, 4);
    if (!rgba) {
        fprintf(stderr, "peacock-splash: failed to load image %s\n", path);
        return -1;
    }
    draw_rgba_image_centered(rgba, w, h);
    stbi_image_free(rgba);
    splash_flush();
    return 0;
}

// 3x5 font for A-Z, 0-9, space, colon, dot, slash, dash
static const unsigned char font3x5[][5] = {
    /* 0 */ {0x7,0x5,0x5,0x5,0x7}, /* 0 */
    /* 1 */ {0x2,0x6,0x2,0x2,0x7}, /* 1 */
    /* 2 */ {0x7,0x1,0x7,0x4,0x7}, /* 2 */
    /* 3 */ {0x7,0x1,0x7,0x1,0x7}, /* 3 */
    /* 4 */ {0x5,0x5,0x7,0x1,0x1}, /* 4 */
    /* 5 */ {0x7,0x4,0x7,0x1,0x7}, /* 5 */
    /* 6 */ {0x7,0x4,0x7,0x5,0x7}, /* 6 */
    /* 7 */ {0x7,0x1,0x2,0x2,0x2}, /* 7 */
    /* 8 */ {0x7,0x5,0x7,0x5,0x7}, /* 8 */
    /* 9 */ {0x7,0x5,0x7,0x1,0x7}, /* 9 */
    /* A */ {0x7,0x5,0x7,0x5,0x5},
    /* B */ {0x6,0x5,0x6,0x5,0x6},
    /* C */ {0x7,0x4,0x4,0x4,0x7},
    /* D */ {0x6,0x5,0x5,0x5,0x6},
    /* E */ {0x7,0x4,0x7,0x4,0x7},
    /* F */ {0x7,0x4,0x7,0x4,0x4},
    /* G */ {0x7,0x4,0x5,0x5,0x7},
    /* H */ {0x5,0x5,0x7,0x5,0x5},
    /* I */ {0x7,0x2,0x2,0x2,0x7},
    /* J */ {0x1,0x1,0x1,0x5,0x7},
    /* K */ {0x5,0x6,0x4,0x6,0x5},
    /* L */ {0x4,0x4,0x4,0x4,0x7},
    /* M */ {0x5,0x7,0x7,0x5,0x5},
    /* N */ {0x5,0x7,0x7,0x7,0x5},
    /* O */ {0x7,0x5,0x5,0x5,0x7},
    /* P */ {0x7,0x5,0x7,0x4,0x4},
    /* Q */ {0x7,0x5,0x5,0x7,0x1},
    /* R */ {0x7,0x5,0x7,0x6,0x5},
    /* S */ {0x7,0x4,0x7,0x1,0x7},
    /* T */ {0x7,0x2,0x2,0x2,0x2},
    /* U */ {0x5,0x5,0x5,0x5,0x7},
    /* V */ {0x5,0x5,0x5,0x5,0x2},
    /* W */ {0x5,0x5,0x7,0x7,0x5},
    /* X */ {0x5,0x5,0x2,0x5,0x5},
    /* Y */ {0x5,0x5,0x2,0x2,0x2},
    /* Z */ {0x7,0x1,0x2,0x4,0x7},
};

static const unsigned char glyph_colon[5] = {0x0,0x2,0x0,0x2,0x0};
static const unsigned char glyph_dot[5]   = {0x0,0x0,0x0,0x0,0x2};
static const unsigned char glyph_slash[5] = {0x1,0x1,0x2,0x4,0x4};
static const unsigned char glyph_dash[5]  = {0x0,0x0,0x7,0x0,0x0};

static const unsigned char *get_glyph(char c) {
    if (c >= '0' && c <= '9') return font3x5[c - '0'];
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') return font3x5[10 + (c - 'A')];
    if (c == ':') return glyph_colon;
    if (c == '.') return glyph_dot;
    if (c == '/') return glyph_slash;
    if (c == '-') return glyph_dash;
    return NULL;
}

static void draw_text_3x5(const char *text, int x, int y, unsigned int color, int scale) {
    int cursor = x;
    for (const char *p = text; *p; p++) {
        if (*p == ' ') {
            cursor += 4 * scale;
            continue;
        }
        const unsigned char *g = get_glyph(*p);
        if (!g) {
            cursor += 4 * scale;
            continue;
        }
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 3; col++) {
                if (g[row] & (1 << (2 - col))) {
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            draw_pixel(cursor + col * scale + sx, y + row * scale + sy, color);
                        }
                    }
                }
            }
        }
        cursor += 4 * scale;
    }
}

int splash_init(const char *fbdev) {
    const char *env_fb = getenv("FBDEV");
    if (env_fb && env_fb[0] != '\0') {
        fbdev = env_fb;
    }

    if (fbdev && fbdev[0] != '\0') {
        fb_fd = open(fbdev, O_RDWR);
        if (fb_fd < 0) {
            return -1;
        }
        fprintf(stderr, "peacock-splash: using fbdev %s\n", fbdev);
    } else {
        // Try common framebuffer devices
        const char *devs[] = {"/dev/graphics/fb0", "/dev/fb0", NULL};
        for (int i = 0; devs[i]; i++) {
            fb_fd = open(devs[i], O_RDWR);
            if (fb_fd >= 0) {
                fprintf(stderr, "peacock-splash: using fbdev %s\n", devs[i]);
                break;
            }
        }
        if (fb_fd < 0) return -1;
    }
    
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        close(fb_fd);
        return -1;
    }
    
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        close(fb_fd);
        return -1;
    }
    
    if (vinfo.bits_per_pixel != 16 && vinfo.bits_per_pixel != 24 && vinfo.bits_per_pixel != 32) {
        close(fb_fd);
        return -1;
    }
    screensize = (long int)vinfo.yres * finfo.line_length;
    bytes_per_pixel = vinfo.bits_per_pixel / 8;
    fprintf(stderr, "peacock-splash: fb %dx%d bpp=%u line_length=%u\n",
            vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, finfo.line_length);
    fprintf(stderr, "peacock-splash: rgb offsets r=%u g=%u b=%u t=%u lengths r=%u g=%u b=%u t=%u\n",
            vinfo.red.offset, vinfo.green.offset, vinfo.blue.offset, vinfo.transp.offset,
            vinfo.red.length, vinfo.green.length, vinfo.blue.length, vinfo.transp.length);
    fprintf(stderr, "peacock-splash: virt %ux%u offset x=%u y=%u\n",
            vinfo.xres_virtual, vinfo.yres_virtual, vinfo.xoffset, vinfo.yoffset);

    // Keep existing scanout buffer by default to preserve lk2nd handover frame.
    // Allow forcing a pan/activate only for debugging.
    if (getenv("PEACOCK_SPLASH_FORCE_PAN")) {
        vinfo.xoffset = 0;
        vinfo.yoffset = 0;
        vinfo.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
        splash_flush();
    }
    open_active_tty();
    fb_mem = (char*)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    
    if (fb_mem == MAP_FAILED) {
        close(fb_fd);
        return -1;
    }
    
    return 0;
}

void splash_clear(unsigned int color) {
    if (!fb_mem) return;
    unsigned int rows = vinfo.yres;
    if (bytes_per_pixel == 2) {
        unsigned short c16 = (unsigned short)pack_pixel(color);
        for (int y = 0; y < (int)rows; y++) {
            unsigned short *row = (unsigned short*)((char*)fb_mem + y * finfo.line_length);
            for (int x = 0; x < (int)vinfo.xres; x++) {
                row[x] = c16;
            }
        }
    } else if (bytes_per_pixel == 3) {
        unsigned int c = pack_pixel(color);
        for (int y = 0; y < (int)rows; y++) {
            unsigned char *row = (unsigned char*)fb_mem + y * finfo.line_length;
            for (int x = 0; x < (int)vinfo.xres; x++) {
                int pos = x * 3;
                row[pos + 0] = (unsigned char)(c & 0xFF);
                row[pos + 1] = (unsigned char)((c >> 8) & 0xFF);
                row[pos + 2] = (unsigned char)((c >> 16) & 0xFF);
            }
        }
    } else {
        unsigned int c32 = pack_pixel(color);
        for (int y = 0; y < (int)rows; y++) {
            unsigned int *row = (unsigned int*)((char*)fb_mem + y * finfo.line_length);
            for (int x = 0; x < (int)vinfo.xres; x++) {
                row[x] = c32;
            }
        }
    }

    splash_flush();
}

void splash_text_simple(const char *text, int y, unsigned int text_rgb) {
    if (!fb_mem) return;
    unsigned int text_col = pack_pixel(text_rgb);
    int start_y = y * 80 + 20;
    int start_x = 20;
    int scale = 4;

    draw_text_3x5(text, start_x, start_y, text_col, scale);
    splash_flush();
}

void splash_close(void) {
    if (fb_mem) {
        munmap(fb_mem, screensize);
        fb_mem = NULL;
    }
    if (fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
    if (tty_fd >= 0) {
        ioctl(tty_fd, KDSETMODE, KD_TEXT);
        close(tty_fd);
        tty_fd = -1;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <message> [y] [fbdev] [clear_hex] [options...]\n", argv[0]);
        fprintf(stderr, "Options: noclear textmode logo glitch text=<RRGGBB> image=<path>\n");
        return 1;
    }
    
    const char *fbdev = NULL;
    const char *image_path = NULL;
    int noclear = 0;
    int textmode = 0;
    int logo = 0;
    int glitch = 0;
    unsigned int text_rgb = 0xFFFF00;
    const char *env_text = getenv("PEACOCK_SPLASH_TEXT_COLOR");
    const char *env_logo = getenv("PEACOCK_SPLASH_LOGO");
    if (env_text && env_text[0] != '\0') {
        text_rgb = parse_rgb_hex(env_text, text_rgb);
    }
    if (env_logo && env_logo[0] != '\0' && strcmp(env_logo, "0") != 0) {
        logo = 1;
    }
    if (argc >= 4) {
        fbdev = argv[3];
    }
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "noclear") == 0) {
            noclear = 1;
            continue;
        }
        if (strcmp(argv[i], "textmode") == 0) {
            textmode = 1;
            continue;
        }
        if (strcmp(argv[i], "logo") == 0) {
            logo = 1;
            continue;
        }
        if (strcmp(argv[i], "glitch") == 0) {
            glitch = 1;
            continue;
        }
        if (strncmp(argv[i], "text=", 5) == 0) {
            text_rgb = parse_rgb_hex(argv[i] + 5, text_rgb);
            continue;
        }
        if (strncmp(argv[i], "image=", 6) == 0) {
            image_path = argv[i] + 6;
            continue;
        }
    }
    if (splash_init(fbdev) < 0) {
        // Framebuffer not available, just print to stderr
        fprintf(stderr, "SPLASH: %s\n", argv[1]);
        return 0;
    }
    
    // Clear to background unless noclear is requested
    unsigned int clear = 0x000000;
    if (argc >= 5) {
        clear = (unsigned int)strtoul(argv[4], NULL, 16);
    }
    if (!noclear) {
        splash_clear(clear);
    }

    if (glitch) {
        splash_glitch_brief();
    }

    if (image_path && image_path[0] != '\0') {
        draw_png_centered(image_path);
    }

    if (logo) {
        draw_centered_logo();
        /* lk2nd clears a bottom strip for board/version text; mirror that behavior. */
        clear_lk2nd_footer_strip();
    }
    
    // Show message (simple text rendering)
    if (argc >= 3) {
        int y = atoi(argv[2]);
        splash_text_simple(argv[1], y, text_rgb);
    } else {
        splash_text_simple(argv[1], 1, text_rgb);
    }
    
    // Keep it visible for a moment if requested
    if (argc >= 4 && strcmp(argv[3], "wait") == 0) {
        sleep(1);
    }
    
    if (textmode) {
        splash_close();
    }
    
    return 0;
}
