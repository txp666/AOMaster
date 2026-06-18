#include "Drv/ssd1306.h"
#include "Drv/ssd1306_font.h"

#define SSD1306_CMD  0x00
#define SSD1306_DATA 0x40
#define SSD1306_PAGE 0xB0
#define SSD1306_PAGES 8U
#define SSD1306_COL_OFFSET 2U

static char s_cache[SSD1306_PAGES][SSD1306_TEXT_COLS];
static uint8_t s_cache_valid;

static const uint8_t s_init_seq[] = {
    0xAE,
    0xD5, 0xF0,
    0xA8, 0x3F,
    0xD3, 0x00,
    0x40,
    0x8D, 0x14,
    0x20, 0x02,
    0xA1,
    0xC8,
    0xDA, 0x12,
    0x81, 0xCF,
    0xD9, 0xF1,
    0xDB, 0x40,
    0xA4,
    0xA6,
    0xAF
};

static char SSD1306_CleanChar(char c)
{
    if(c < 32 || c > 126)
        return ' ';
    return c;
}

static void SSD1306_CacheSpaces(void)
{
    uint8_t page;
    uint8_t col;

    for(page = 0; page < SSD1306_PAGES; page++)
    {
        for(col = 0; col < SSD1306_TEXT_COLS; col++)
            s_cache[page][col] = ' ';
    }
    s_cache_valid = 1;
}

static void SSD1306_SetPageCol(uint8_t page, uint8_t col)
{
    uint8_t hw_col = col + SSD1306_COL_OFFSET;

    OLED_I2C_Start(SSD1306_I2C_ADDR);
    OLED_I2C_Write(SSD1306_CMD);
    OLED_I2C_Write(SSD1306_PAGE | page);
    OLED_I2C_Write(hw_col & 0x0F);
    OLED_I2C_Write(0x10 | (hw_col >> 4));
    OLED_I2C_Stop();
}

static void SSD1306_WriteCharData(char c)
{
    uint8_t i;

    c = SSD1306_CleanChar(c);
    for(i = 0; i < 5; i++)
        OLED_I2C_Write(SSD1306_Font5x8[(uint8_t)c - 32][i]);
    OLED_I2C_Write(0x00);
}

static void SSD1306_ClearPage(uint8_t page)
{
    uint8_t i;

    SSD1306_SetPageCol(page, 0);
    OLED_I2C_Start(SSD1306_I2C_ADDR);
    OLED_I2C_Write(SSD1306_DATA);
    for(i = 0; i < SSD1306_WIDTH; i++)
        OLED_I2C_Write(0x00);
    OLED_I2C_Stop();
}

uint8_t SSD1306_Init(void)
{
    uint8_t i;
    uint8_t buf[1 + sizeof(s_init_seq)];
    uint8_t n = 0;

    OLED_I2C_Init();
    buf[n++] = SSD1306_CMD;
    for(i = 0; i < sizeof(s_init_seq); i++)
        buf[n++] = s_init_seq[i];

    if(OLED_I2C_Start(SSD1306_I2C_ADDR))
        return 1;
    for(i = 0; i < n; i++)
    {
        if(OLED_I2C_Write(buf[i]))
        {
            OLED_I2C_Stop();
            return 1;
        }
    }
    OLED_I2C_Stop();
    SSD1306_Clear();
    return 0;
}

void SSD1306_SetContrast(uint8_t val)
{
    OLED_I2C_Start(SSD1306_I2C_ADDR);
    OLED_I2C_Write(SSD1306_CMD);
    OLED_I2C_Write(0x81);
    OLED_I2C_Write(val);
    OLED_I2C_Stop();
}

void SSD1306_Clear(void)
{
    uint8_t i;

    for(i = 0; i < SSD1306_PAGES; i++)
        SSD1306_ClearPage(i);
    SSD1306_CacheSpaces();
}

void SSD1306_WriteLine(uint8_t page, const char *text)
{
    char line[SSD1306_TEXT_COLS];
    uint8_t col;
    uint8_t changed = 0;

    if(page >= SSD1306_PAGES)
        return;

    for(col = 0; col < SSD1306_TEXT_COLS; col++)
    {
        char c = ' ';
        if(text && *text)
            c = *text++;
        line[col] = SSD1306_CleanChar(c);
        if(!s_cache_valid || s_cache[page][col] != line[col])
            changed = 1;
    }

    if(!changed)
        return;

    SSD1306_SetPageCol(page, 0);
    OLED_I2C_Start(SSD1306_I2C_ADDR);
    OLED_I2C_Write(SSD1306_DATA);
    for(col = 0; col < SSD1306_TEXT_COLS; col++)
    {
        SSD1306_WriteCharData(line[col]);
        s_cache[page][col] = line[col];
    }
    OLED_I2C_Write(0x00);
    OLED_I2C_Write(0x00);
    OLED_I2C_Stop();
    s_cache_valid = 1;
}

static uint8_t SSD1306_Expand4x2(uint8_t bits)
{
    bits &= 0x0FU;
    bits = (uint8_t)((bits | (uint8_t)(bits << 2U)) & 0x33U);
    bits = (uint8_t)((bits | (uint8_t)(bits << 1U)) & 0x55U);
    return (uint8_t)(bits | (uint8_t)(bits << 1U));
}

static void SSD1306_WriteChar2xData(char c, uint8_t high)
{
    uint8_t i;
    uint8_t bits;

    for(i = 0U; i < 5U; i++)
    {
        bits = SSD1306_Font5x8[(uint8_t)c - 32U][i];
        if(high)
            bits >>= 4U;
        bits = SSD1306_Expand4x2(bits);
        OLED_I2C_Write(bits);
        OLED_I2C_Write(bits);
    }
    OLED_I2C_Write(0x00);
    OLED_I2C_Write(0x00);
}

void SSD1306_WriteLine2x(uint8_t page, const char *text)
{
    uint8_t len = 0U;
    uint8_t left;
    uint8_t p;
    uint8_t i;

    while(text[len])
        len++;

    left = (uint8_t)((SSD1306_WIDTH - (uint8_t)(len * SSD1306_CHAR_W * 2U)) / 2U);
    for(p = 0U; p < 2U; p++)
    {
        SSD1306_ClearPage((uint8_t)(page + p));
        SSD1306_SetPageCol((uint8_t)(page + p), left);
        OLED_I2C_Start(SSD1306_I2C_ADDR);
        OLED_I2C_Write(SSD1306_DATA);
        for(i = 0U; i < len; i++)
            SSD1306_WriteChar2xData(text[i], p);
        OLED_I2C_Stop();
    }
    s_cache[page][0] = s_cache[page + 1U][0] = 0;
}
