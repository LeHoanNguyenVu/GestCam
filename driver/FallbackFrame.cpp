#include "driver/FallbackFrame.h"
#include <cmath>
#include <cstring>
#include <string>

namespace gestcam::driver {

// Simple 5x7 bitmap font for uppercase ASCII [32..90]
// 5 columns per character, 7 bits high (MSB at top)
static const uint8_t FONT_5X7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' ' (32)
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // '%'
    {0x36, 0x49, 0x55, 0x22, 0x50}, // '&'
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // ')'
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // '+'
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // '1'
    {0x42, 0x61, 0x51, 0x49, 0x46}, // '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // '6'
    {0x01, 0x71, 0x09, 0x05, 0x03}, // '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // '9'
    {0x00, 0x36, 0x36, 0x00, 0x00}, // ':'
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ';'
    {0x08, 0x14, 0x22, 0x41, 0x00}, // '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // '='
    {0x00, 0x41, 0x22, 0x14, 0x08}, // '>'
    {0x02, 0x01, 0x51, 0x09, 0x06}, // '?'
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // '@'
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 'L'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 'R'
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 'V'
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 'X'
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43}  // 'Z'
};

static inline void SetPixel(uint8_t* out, int width, int height, int x, int y, uint8_t b, uint8_t g, uint8_t r) {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        size_t idx = (static_cast<size_t>(y) * width + x) * 3;
        out[idx + 0] = b;
        out[idx + 1] = g;
        out[idx + 2] = r;
    }
}

static void DrawChar(uint8_t* out, int width, int height, int start_x, int start_y, char c, int scale, uint8_t b, uint8_t g, uint8_t r) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c < 32 || c > 90) c = ' ';
    const uint8_t* glyph = FONT_5X7[c - 32];

    for (int col = 0; col < 5; ++col) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 7; ++row) {
            if (line & (1 << row)) {
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        SetPixel(out, width, height, start_x + col * scale + dx, start_y + row * scale + dy, b, g, r);
                    }
                }
            }
        }
    }
}

static void DrawText(uint8_t* out, int width, int height, int x, int y, const std::string& text, int scale, uint8_t b, uint8_t g, uint8_t r) {
    int cur_x = x;
    int spacing = (5 + 1) * scale;
    for (char c : text) {
        DrawChar(out, width, height, cur_x, y, c, scale, b, g, r);
        cur_x += spacing;
    }
}

void RenderFallbackFrame(uint8_t* out_rgb24, int width, int height, uint64_t frame_tick) {
    if (!out_rgb24 || width <= 0 || height <= 0) return;

    // 1. Dark elegant gradient background
    for (int y = 0; y < height; ++y) {
        float t = static_cast<float>(y) / height;
        uint8_t r = static_cast<uint8_t>(16 + t * 12);
        uint8_t g = static_cast<uint8_t>(20 + t * 14);
        uint8_t b = static_cast<uint8_t>(32 + t * 24);

        size_t row_start = static_cast<size_t>(y) * width * 3;
        for (int x = 0; x < width; ++x) {
            out_rgb24[row_start + x * 3 + 0] = b;
            out_rgb24[row_start + x * 3 + 1] = g;
            out_rgb24[row_start + x * 3 + 2] = r;
        }
    }

    // 2. Centered stylized container box
    int box_w = 640;
    int box_h = 240;
    int box_x = (width - box_w) / 2;
    int box_y = (height - box_h) / 2;

    for (int y = box_y; y < box_y + box_h; ++y) {
        for (int x = box_x; x < box_x + box_w; ++x) {
            // Draw subtle border and darker container background
            bool is_border = (x == box_x || x == box_x + box_w - 1 || y == box_y || y == box_y + box_h - 1);
            if (is_border) {
                SetPixel(out_rgb24, width, height, x, y, 120, 80, 40);
            } else {
                SetPixel(out_rgb24, width, height, x, y, 42, 28, 20);
            }
        }
    }

    // 3. Render Header Text: "GESTCAM VIRTUAL CAMERA"
    std::string title = "GESTCAM VIRTUAL CAMERA";
    int title_scale = 4;
    int title_w = static_cast<int>(title.size()) * 6 * title_scale;
    int title_x = (width - title_w) / 2;
    int title_y = box_y + 40;
    DrawText(out_rgb24, width, height, title_x, title_y, title, title_scale, 255, 220, 100);

    // 4. Render Subtitle Text: "STANDBY - WAITING FOR CORE"
    std::string subtitle = "STANDBY - WAITING FOR CORE";
    int sub_scale = 2;
    int sub_w = static_cast<int>(subtitle.size()) * 6 * sub_scale;
    int sub_x = (width - sub_w) / 2;
    int sub_y = box_y + 120;
    DrawText(out_rgb24, width, height, sub_x, sub_y, subtitle, sub_scale, 180, 180, 180);

    // 5. Pulsing activity dot
    float pulse = 0.5f + 0.5f * std::sin(frame_tick * 0.1f);
    int dot_r = static_cast<int>(5 + pulse * 4);
    int dot_cx = width / 2;
    int dot_cy = box_y + 180;
    uint8_t dot_b = static_cast<uint8_t>(255 * pulse);
    uint8_t dot_g = static_cast<uint8_t>(180 * pulse);
    uint8_t dot_r_col = 0;

    for (int dy = -dot_r; dy <= dot_r; ++dy) {
        for (int dx = -dot_r; dx <= dot_r; ++dx) {
            if (dx * dx + dy * dy <= dot_r * dot_r) {
                SetPixel(out_rgb24, width, height, dot_cx + dx, dot_cy + dy, dot_b, dot_g, dot_r_col);
            }
        }
    }
}

} // namespace gestcam::driver
