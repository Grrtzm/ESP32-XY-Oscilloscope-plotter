// Hieronder een simpele ESP-IDF v6.0.1 DMA gebaseerde vector demo voor klassieke ESP32:

// GPIO25 / DAC_CHAN_0 → X
// GPIO26 / DAC_CHAN_1 → Y
// GPIO27 → blanking / Z-input

// met BOOT-knop GPIO0 om door testbeelden te stappen.

// Testbeelden:

// 1. tekst "Hallo Martti!!"
// 2. dobbelsteen 5 stippen
// 3. blanking test om met potmeter te testen (rechter stip moet uitgaan)
// 4. horizontale lijn
// 5. verticale lijn
// 6. diagonale lijn
// 7. vierkant
// 8. cirkel
// 9. lissajous figuur

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/dac_continuous.h"

#include "esp_check.h"
#include "esp_timer.h"

#define BOOT_GPIO GPIO_NUM_0
#define SAMPLE_RATE_HZ 100000
#define BUFFER_PAIRS 4096
#define ACTIVE_PAIRS 2048

#define X_MIN 20
#define X_MAX 235
#define Y_MIN 20
#define Y_MAX 235

#define PI 3.14159265358979323846f

#define X_BLANK 255 // softwarematige "beam off" code.

static dac_continuous_handle_t dac_handle;
static uint8_t xy_buf[BUFFER_PAIRS * 2];
static int write_pos = 0;

static size_t active_bytes = 4096;

typedef enum
{
    MODE_TEXT = 0,
    MODE_FIVE_DOTS,
    MODE_BLANK_TEST,
    MODE_H_LINE,
    MODE_V_LINE,
    MODE_DIAG,
    MODE_SQUARE,
    MODE_CIRCLE,
    MODE_LISSAJOUS,
    MODE_COUNT
} display_mode_t;

static display_mode_t mode = MODE_TEXT; // MODE_FIVE_DOTS;

static inline void put_xy(uint8_t x, uint8_t y)
{
    if (write_pos >= BUFFER_PAIRS)
        return;

    xy_buf[2 * write_pos + 0] = x; // GPIO25 / X
    xy_buf[2 * write_pos + 1] = y; // GPIO26 / Y
    write_pos++;
}

static inline void put_xy_blank(uint8_t y)
{
    put_xy(X_BLANK, y);
}

static inline void put_xy_visible(uint8_t x, uint8_t y)
{
    if (x > 235)
        x = 235;
    put_xy(x, y);
}

static void clear_buffer(void)
{
    write_pos = 0;

    for (int i = 0; i < BUFFER_PAIRS; i++)
    {
        xy_buf[2 * i + 0] = 128;
        xy_buf[2 * i + 1] = 128;
    }
}

static void fill_remaining_with_last(void)
{
    uint8_t x = 128;
    uint8_t y = 128;

    if (write_pos > 0)
    {
        x = xy_buf[2 * (write_pos - 1) + 0];
        y = xy_buf[2 * (write_pos - 1) + 1];
    }

    while (write_pos < BUFFER_PAIRS)
    {
        put_xy(x, y);
    }
}

static void draw_line(uint8_t x1, uint8_t y1,
                      uint8_t x2, uint8_t y2)
{
    int dx = abs((int)x2 - (int)x1);
    int dy = abs((int)y2 - (int)y1);

    int steps = (dx > dy) ? dx : dy;
    steps = steps / 3;

    if (steps < 3)
        steps = 3;

    for (int i = 0; i <= steps; i++)
    {
        uint8_t x = x1 + ((int)x2 - (int)x1) * i / steps;
        uint8_t y = y1 + ((int)y2 - (int)y1) * i / steps;

        put_xy_visible(x, y);
    }
}

static void move_blank(uint8_t y, int samples)
{
    for (int i = 0; i < samples; i++)
    {
        put_xy_blank(y);
    }
}

static void make_five_dots(void)
{
    clear_buffer();

    const uint8_t pts[5][2] = {
        {X_MIN, Y_MIN},
        {X_MAX, Y_MIN},
        {128, 128},
        {X_MIN, Y_MAX},
        {X_MAX, Y_MAX},
    };

    for (int i = 0; i < BUFFER_PAIRS; i++)
    {
        int p = (i / 128) % 5;
        put_xy(pts[p][0], pts[p][1]);
    }
}

static void make_h_line(void)
{
    clear_buffer();
    for (int i = 0; i < BUFFER_PAIRS; i++)
    {
        uint8_t x = X_MIN + ((X_MAX - X_MIN) * (i % 256)) / 255;
        put_xy(x, 128);
    }
}

static void make_v_line(void)
{
    clear_buffer();
    for (int i = 0; i < BUFFER_PAIRS; i++)
    {
        uint8_t y = Y_MIN + ((Y_MAX - Y_MIN) * (i % 256)) / 255;
        put_xy(128, y);
    }
}

static void make_diag(void)
{
    clear_buffer();
    for (int i = 0; i < BUFFER_PAIRS; i++)
    {
        uint8_t v = 20 + (215 * (i % 256)) / 255;
        put_xy(v, v);
    }
}

static void make_square(void)
{
    clear_buffer();

    while (write_pos < BUFFER_PAIRS - 1024)
    {
        draw_line(X_MIN, Y_MIN, X_MAX, Y_MIN);
        draw_line(X_MAX, Y_MIN, X_MAX, Y_MAX);
        draw_line(X_MAX, Y_MAX, X_MIN, Y_MAX);
        draw_line(X_MIN, Y_MAX, X_MIN, Y_MIN);
    }

    fill_remaining_with_last();
}

static void make_circle(void)
{
    clear_buffer();

    const float cx = 128.0f;
    const float cy = 128.0f;
    const float r = 105.0f;

    for (int i = 0; i < ACTIVE_PAIRS; i++)
    {
        float a = 2.0f * PI * (float)i / (float)ACTIVE_PAIRS;
        uint8_t x = (uint8_t)(cx + r * cosf(a));
        uint8_t y = (uint8_t)(cy + r * sinf(a));

        xy_buf[2 * i + 0] = x;
        xy_buf[2 * i + 1] = y;
    }

    write_pos = BUFFER_PAIRS;
}

typedef struct
{
    int x1, y1, x2, y2;
} stroke_t;

#define S(x1, y1, x2, y2) {x1, y1, x2, y2}

static const stroke_t G_H[] = {S(0, 0, 0, 10), S(6, 0, 6, 10), S(0, 5, 6, 5)};
static const stroke_t G_a[] = {S(1, 10, 1, 5), S(1, 5, 3, 4), S(3, 4, 5, 5), S(5, 5, 5, 10), S(1, 7, 5, 7)};
static const stroke_t G_l[] = {S(3, 0, 3, 10)};
static const stroke_t G_o[] = {S(1, 4, 5, 4), S(5, 4, 6, 5), S(6, 5, 6, 9), S(6, 9, 5, 10), S(5, 10, 1, 10), S(1, 10, 0, 9), S(0, 9, 0, 5), S(0, 5, 1, 4)};
static const stroke_t G_M[] = {S(0, 10, 0, 0), S(0, 0, 3, 5), S(3, 5, 6, 0), S(6, 0, 6, 10)};
static const stroke_t G_r[] = {S(0, 4, 0, 10), S(0, 5, 2, 4), S(2, 4, 6, 4)};
static const stroke_t G_t[] = {S(3, 1, 3, 9), S(0, 4, 6, 4), S(3, 9, 5, 10)};
static const stroke_t G_i[] = {S(3, 4, 3, 10), S(3, 1, 3, 2)};
static const stroke_t G_exc[] = {S(3, 0, 3, 7), S(3, 9, 3, 10)};

typedef struct
{
    char c;
    const stroke_t *s;
    int n;
    int adv;
} glyph_t;

static const glyph_t font[] = {
    {'H', G_H, sizeof(G_H) / sizeof(G_H[0]), 8},
    {'a', G_a, sizeof(G_a) / sizeof(G_a[0]), 8},
    {'l', G_l, sizeof(G_l) / sizeof(G_l[0]), 5},
    {'o', G_o, sizeof(G_o) / sizeof(G_o[0]), 8},
    {'M', G_M, sizeof(G_M) / sizeof(G_M[0]), 9},
    {'r', G_r, sizeof(G_r) / sizeof(G_r[0]), 8},
    {'t', G_t, sizeof(G_t) / sizeof(G_t[0]), 8},
    {'i', G_i, sizeof(G_i) / sizeof(G_i[0]), 5},
    {'!', G_exc, sizeof(G_exc) / sizeof(G_exc[0]), 4},
};

static const glyph_t *find_glyph(char c)
{
    for (int i = 0; i < sizeof(font) / sizeof(font[0]); i++)
    {
        if (font[i].c == c)
            return &font[i];
    }
    return NULL;
}

static void make_text(void)
{
    clear_buffer();

    const char *line1 = "Hallo";
    const char *line2 = "Martti!!";

    const char *lines[2] = {line2, line1};

    const int x0 = 35;
    const int y_base[2] = {35, 130};

    for (int line = 0; line < 2; line++)
    {
        const char *text = lines[line];

        int total = 0;
        for (const char *p = text; *p; p++)
        {
            if (*p == ' ')
            {
                total += 5;
            }
            else
            {
                const glyph_t *g = find_glyph(*p);
                if (g)
                    total += g->adv;
            }
        }

        float sx = 185.0f / (float)total;
        float sy = 75.0f / 10.0f;

        float cursor = 0.0f;

        for (const char *p = text; *p; p++)
        {
            if (*p == ' ')
            {
                cursor += 5;
                continue;
            }

            const glyph_t *g = find_glyph(*p);
            if (!g)
                continue;

            for (int i = 0; i < g->n; i++)
            {
                const stroke_t *s = &g->s[i];

                uint8_t x1 = x0 + (uint8_t)((cursor + s->x1) * sx);
                uint8_t x2 = x0 + (uint8_t)((cursor + s->x2) * sx);

                uint8_t y1 = y_base[line] + (uint8_t)((10 - s->y1) * sy);
                uint8_t y2 = y_base[line] + (uint8_t)((10 - s->y2) * sy);

                move_blank(y1, 0); // was 4
                draw_line(x1, y1, x2, y2);
            }

            cursor += g->adv;
        }
    }

    // Geen felle eindstip: resterende samples blanken
    while (write_pos < ACTIVE_PAIRS )
    {
        put_xy_blank(0);
    }
}

static void make_lissajous(void)
{
    clear_buffer();

    static float phase = 0.0f;

    const float cx = 128.0f;
    const float cy = 128.0f;

    const float ax = 95.0f;
    const float ay = 95.0f;

    // 3:2 Lissajous, met langzaam verschuivende fase
    const float fx = 3.0f;
    const float fy = 2.0f;

    const int pairs = ACTIVE_PAIRS; // bij jou meestal 2048

    for (int i = 0; i < pairs; i++)
    {
        float t = 2.0f * PI * (float)i / (float)pairs;

        uint8_t x = (uint8_t)(cx + ax * sinf(fx * t + phase));
        uint8_t y = (uint8_t)(cy + ay * sinf(fy * t));

        put_xy_visible(x, y);
    }

    phase += 0.08f;
    if (phase > 2.0f * PI)
    {
        phase -= 2.0f * PI;
    }

    fill_remaining_with_last();
}

static void make_blank_test(void)
{
    clear_buffer();

    for (int i = 0; i < ACTIVE_PAIRS; i++)
    {
        int block = (i / 128) % 2;

        if (block == 0)
        {
            // normale zichtbare horizontale lijn
            put_xy_visible(80, 128);
        }
        else
        {
            // blanking-code: X maximaal
            put_xy(255, 128);
        }
    }

    fill_remaining_with_last();
}

static void make_pattern(void)
{
    switch (mode)
    {
    case MODE_FIVE_DOTS:
        make_five_dots();
        break;
    case MODE_BLANK_TEST:
        make_blank_test();
        break;
    case MODE_H_LINE:
        make_h_line();
        break;
    case MODE_V_LINE:
        make_v_line();
        break;
    case MODE_DIAG:
        make_diag();
        break;
    case MODE_SQUARE:
        make_square();
        break;
    case MODE_CIRCLE:
        make_circle();
        break;
    case MODE_TEXT:
        make_text();
        break;
    case MODE_LISSAJOUS:
        make_lissajous();
        break;
    default:
        make_five_dots();
        break;
    }
}

static void load_pattern(void)
{
    size_t bytes_loaded = 0;

    vTaskDelay(pdMS_TO_TICKS(1)); // watchdog/driver lucht geven
    make_pattern();
    vTaskDelay(pdMS_TO_TICKS(1));

    ESP_ERROR_CHECK(dac_continuous_write_cyclically(
        dac_handle,
        xy_buf,
        sizeof(xy_buf),
        &bytes_loaded));

    active_bytes = bytes_loaded;

    printf("mode=%d buf=%u bytes_loaded=%u ACTIVE_PAIRS =%d\n",
           mode,
           (unsigned)sizeof(xy_buf),
           (unsigned)bytes_loaded,
           ACTIVE_PAIRS);
}

static void check_boot_button(void)
{
    static int last = 1;
    static int64_t last_us = 0;

    int now_level = gpio_get_level(BOOT_GPIO);
    int64_t now_us = esp_timer_get_time();

    if (last == 1 && now_level == 0)
    {
        if ((now_us - last_us) > 300000)
        {
            mode = (mode + 1) % MODE_COUNT;
            load_pattern();
            last_us = now_us;
        }
    }

    last = now_level;
}

void app_main(void)
{
    gpio_config_t boot_conf = {
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_conf));

    dac_continuous_config_t cfg = {
        .chan_mask = DAC_CHANNEL_MASK_ALL,
        .desc_num = 16, // 8,
        .buf_size = 512,
        .freq_hz = SAMPLE_RATE_HZ,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,
        .chan_mode = DAC_CHANNEL_MODE_ALTER,
    };

    ESP_ERROR_CHECK(dac_continuous_new_channels(&cfg, &dac_handle));
    ESP_ERROR_CHECK(dac_continuous_enable(dac_handle));

    load_pattern();

    while (1)
    {
        check_boot_button();
        vTaskDelay(pdMS_TO_TICKS(20));

        if (mode == MODE_LISSAJOUS)
        {
            load_pattern();
            vTaskDelay(pdMS_TO_TICKS(40)); // ongeveer 25 fps animatie
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}
