#define NDEBUG
#define CHIPS_IMPL

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h> // for usleep

#include <badgevms/framebuffer.h>
#include <badgevms/compositor.h>
#include <badgevms/keyboard.h>
#include <badgevms/pixel_formats.h>

#include "chips/chips/chips_common.h"
#include "chips/chips/m6502.h"
#include "chips/chips/m6526.h"
#include "chips/chips/m6569.h"
#include "chips/chips/m6581.h"
#include "chips/chips/beeper.h"
#include "chips/chips/kbd.h"
#include "chips/chips/mem.h"
#include "chips/chips/clk.h"
#include "chips/systems/c1530.h"
#include "chips/chips/m6522.h"
#include "chips/systems/c1541.h"
#include "chips/systems/c64.h"
#include "chips-test/examples/roms/c64-roms.h"

#define FB_WIDTH  (392)
#define FB_HEIGHT (272)
#define W_WIDTH   (640)
#define W_HEIGHT  (400)

#define FRAME_USEC (16667)

// ctype.h implementations, workaround for missing symbols
int islower(int c) {
    return c >= 'a' && c <= 'z';
}

int isupper(int c) {
    return c >= 'A' && c <= 'Z';
}

int tolower(int c) {
    return c + ('a' - 'A');
}

int toupper(int c) {
    return c - ('a' - 'A');
}

int main()
{
    printf("[c64emu] starting...\n");
    long const target_frame_time_us = FRAME_USEC;

    // window setup
    window_size_t size = { .w = W_WIDTH, .h = W_HEIGHT };
    window_handle_t window = window_create(
        "C64",
        size,
        0
    );

    window_flag_t flags = window_flags_get(window);
    window_flags_set(window, flags ^ WINDOW_FLAG_FULLSCREEN);

    // framebuffer setup
    size.w = FB_WIDTH;
    size.h = FB_HEIGHT;
    framebuffer_t* framebuffer = window_framebuffer_create(
        window,
        size,
        BADGEVMS_PIXELFORMAT_RGBA8888
    );

    // initialize emulator
    c64_t* emulator = malloc(sizeof(c64_t));

    if (emulator == NULL) {
        printf("[c64emu] failed to allocate memory...\n");
        return 1;
    }

    c64_init(emulator, &(c64_desc_t){
        .c1530_enabled = true,
        .audio = { .num_samples = 0 },
        .roms = {
            .chars = {
                .ptr=dump_c64_char_bin,
                .size=sizeof(dump_c64_char_bin)
            },
            .basic = {
                .ptr=dump_c64_basic_bin,
                .size=sizeof(dump_c64_basic_bin)
            },
            .kernal = {
                .ptr=dump_c64_kernalv3_bin,
                .size=sizeof(dump_c64_kernalv3_bin)
            }
        }
    });

    // initialize rendering constants
    const chips_display_info_t display_info = c64_display_info(emulator);
    const int w = display_info.screen.width;
    const int h = display_info.screen.height;
    const int off_x = display_info.screen.x;
    const int off_y = display_info.screen.y;
    const int row_pitch = display_info.frame.dim.width; // full width of fb line
    const uint32_t* pal = display_info.palette.ptr;
    uint32_t* host_pixels = (uint32_t*)framebuffer->pixels;

    printf("[c64emu] initialized emulator...\n");

    while (1) {
        struct timespec start_time, end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        // run emulation for one frame
        c64_exec(emulator, FRAME_USEC);

        for (size_t y = 0; y < h; y++) {
            for (size_t x = 0; x < w; x++) {
                const size_t i = (y + off_y) * row_pitch + (x + off_x);
                const uint8_t pal_index = emulator->vic.crt.fb[i] & 0xF;

                host_pixels[y * FB_WIDTH + x] = pal[pal_index];
            }
        }

        // keyboard input
        event_t e = window_event_poll(window, false, 0);
        if (e.type == EVENT_KEY_DOWN) {
            keyboard_scancode_t scancode = e.keyboard.scancode;

            if (scancode == KEY_SCANCODE_ESCAPE) {
                break;
            }

            char ch = keyboard_get_ascii(scancode, 0);

            switch (ch) {
                case KEY_SCANCODE_RETURN:       ch = 0x0D; break; // ENTER
                case KEY_SCANCODE_BACKSPACE:    ch = 0x01; break; // BACKSPACE
                case KEY_SCANCODE_ESCAPE:       ch = 0x03; break; // ESCAPE
                case KEY_SCANCODE_LEFT:         ch = 0x08; break; // LEFT
                case KEY_SCANCODE_RIGHT:        ch = 0x09; break; // RIGHT
                case KEY_SCANCODE_UP:           ch = 0x0B; break; // UP
                case KEY_SCANCODE_DOWN:         ch = 0x0A; break; // DOWN
            }

            if (ch > 32) {
                if (islower(ch)) {
                    ch = toupper(ch);
                }
                else if (isupper(ch)) {
                    ch = tolower(ch);
                }
            }

            c64_key_down(emulator, ch);
            c64_key_up(emulator, ch);
        }

        window_present(window, false, NULL, 0);

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        long elapsed_time_us = (end_time.tv_sec * 1000000L - start_time.tv_sec * 1000000L) +
                               (end_time.tv_nsec / 1000L - start_time.tv_nsec / 1000L);

        long sleep_time = target_frame_time_us - elapsed_time_us;

        if (sleep_time > 0) {
            usleep(sleep_time);
        }
        //printf("elapsed us: %lu\n", elapsed_us);
    }

    free(emulator);
    printf("[c64emu] exiting...\n");

    return 0;
}
