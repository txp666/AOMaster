#include "App/ui.h"
#include "App/settings.h"
#include "Drv/ssd1306.h"

#define UI_COLS SSD1306_TEXT_COLS

static char s_line[UI_COLS + 1U];
static uint8_t s_pos;

static void Ui_LineBegin(void)
{
    uint8_t i;

    for(i = 0; i < UI_COLS; i++)
        s_line[i] = ' ';
    s_line[UI_COLS] = 0;
    s_pos = 0;
}

static void Ui_LineFlush(uint8_t page)
{
    SSD1306_WriteLine(page, s_line);
}

static void Ui_PutChar(char c)
{
    if(s_pos < UI_COLS)
        s_line[s_pos++] = c;
}

static void Ui_PutStr(const char *str)
{
    while(*str)
        Ui_PutChar(*str++);
}

static void Ui_WriteText(uint8_t page, const char *text)
{
    SSD1306_WriteLine(page, text);
}

static void Ui_WriteBlank(uint8_t page)
{
    SSD1306_WriteLine(page, "");
}

static void Ui_PutDec(uint32_t value)
{
    char tmp[10];
    uint8_t n = 0;

    do
    {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while(value && n < sizeof(tmp));

    while(n)
        Ui_PutChar(tmp[--n]);
}

static void Ui_PutDecWidth(uint32_t value, uint8_t width)
{
    char tmp[5];
    uint8_t n = 0;

    do
    {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while(value && n < sizeof(tmp));

    while(n < width && n < sizeof(tmp))
        tmp[n++] = '0';

    while(n)
        Ui_PutChar(tmp[--n]);
}

static void Ui_PutMode(GP8630_OutputMode_t mode)
{
    Ui_PutStr((mode == GP8630_OUT_CURR_20MA) ? "4-20mA" : "0-10V");
}

static void Ui_PutPercent(int16_t permille)
{
    uint16_t v;

    if(permille < 0)
    {
        Ui_PutChar('-');
        v = (uint16_t)(-permille);
    }
    else
    {
        v = (uint16_t)permille;
    }

    v = (uint16_t)((v + (SETTINGS_PERMILLE_SCALE / 2)) / SETTINGS_PERMILLE_SCALE);
    Ui_PutDec((uint32_t)(v / 10U));
    Ui_PutChar('.');
    Ui_PutDec((uint32_t)(v % 10U));
    Ui_PutChar('%');
}

static void Ui_PutCode(uint16_t code)
{
    Ui_PutDecWidth(code, 5U);
}

static void Ui_PutSignedValue(int32_t value, const char *unit, uint8_t decimals)
{
    uint32_t v;

    if(value < 0)
    {
        Ui_PutChar('-');
        v = (uint32_t)(-value);
    }
    else
    {
        v = (uint32_t)value;
    }

    Ui_PutDec(v / 1000U);
    Ui_PutChar('.');
    if(decimals == 3U)
        Ui_PutDecWidth(v % 1000U, 3U);
    else
        Ui_PutDecWidth((v % 1000U) / 10U, 2U);
    Ui_PutStr(unit);
}

static void Ui_PutValue(int16_t permille, GP8630_OutputMode_t mode)
{
    int32_t v;

    if(mode == GP8630_OUT_CURR_20MA)
    {
        v = 4000 + ((int32_t)permille * 16000) / SETTINGS_PERMILLE_BASE;
        Ui_PutSignedValue(v, "mA", 2U);
    }
    else
    {
        v = ((int32_t)permille * 10000) / SETTINGS_PERMILLE_BASE;
        Ui_PutSignedValue(v, "V", 2U);
    }
}

static void Ui_PutStepValue(uint16_t step_permille, GP8630_OutputMode_t mode)
{
    uint32_t v;

    if(mode == GP8630_OUT_CURR_20MA)
    {
        v = ((uint32_t)step_permille * 16000U) / SETTINGS_PERMILLE_BASE;
        Ui_PutDec(v / 1000U);
        Ui_PutChar('.');
        Ui_PutDecWidth(v % 1000U, 3U);
        Ui_PutStr("mA");
    }
    else
    {
        v = ((uint32_t)step_permille * 10000U) / SETTINGS_PERMILLE_BASE;
        Ui_PutDec(v / 1000U);
        Ui_PutChar('.');
        Ui_PutDecWidth((v % 1000U) / 10U, 2U);
        Ui_PutChar('V');
    }
}

static void Ui_PutHex8(uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    Ui_PutChar(hex[(value >> 4) & 0x0FU]);
    Ui_PutChar(hex[value & 0x0FU]);
}

static uint8_t Ui_BarFill(int16_t permille)
{
    if(permille < 0)
        permille = 0;
    if(permille > SETTINGS_PERMILLE_BASE)
        permille = SETTINGS_PERMILLE_BASE;
    return (uint8_t)(permille / (SETTINGS_PERMILLE_BASE / 10));
}

static void Ui_PutBar(uint8_t fill)
{
    uint8_t i;

    Ui_PutChar('[');
    for(i = 0; i < 10U; i++)
        Ui_PutChar(i < fill ? '#' : '-');
    Ui_PutChar(']');
}

static void Ui_WriteMenuItem(uint8_t page, uint8_t active, const char *text)
{
    Ui_LineBegin();
    Ui_PutStr(active ? "> " : "  ");
    Ui_PutStr(text);
    Ui_LineFlush(page);
}

static void Ui_PutSignalMode(uint8_t mode)
{
    Ui_PutStr(mode ? "0-10V" : "4-20mA");
}

static void Ui_PutWave(uint8_t waveform)
{
    switch(waveform)
    {
    case 1:
        Ui_PutStr("STEP");
        break;
    case 2:
        Ui_PutStr("RAMP");
        break;
    case 3:
        Ui_PutStr("SQR");
        break;
    case 4:
        Ui_PutStr("TRI");
        break;
    case 5:
        Ui_PutStr("SINE");
        break;
    default:
        Ui_PutStr("RAMP");
        break;
    }
}

static void Ui_PutSignalValue(uint8_t mode, uint16_t raw)
{
    Ui_PutSignedValue(raw, mode ? "V" : "mA", 2U);
}

static void Ui_PutSignalItem(uint8_t item, uint8_t active, uint8_t mode, uint8_t waveform, uint16_t low, uint16_t high, uint16_t period)
{
    switch(item)
    {
    case 0:
        Ui_PutStr("Run:");
        Ui_PutStr(active ? "ON" : "OFF");
        break;
    case 1:
        Ui_PutStr("Type:");
        Ui_PutSignalMode(mode);
        break;
    case 2:
        Ui_PutStr("Wave:");
        Ui_PutWave(waveform);
        break;
    case 3:
        Ui_PutStr("Low:");
        Ui_PutSignalValue(mode, low);
        break;
    case 4:
        Ui_PutStr("High:");
        Ui_PutSignalValue(mode, high);
        break;
    case 5:
        Ui_PutStr("Per:");
        Ui_PutDec(period);
        Ui_PutStr("ms");
        break;
    default:
        break;
    }
}

static void Ui_WriteSignalItem(uint8_t page, uint8_t item, uint8_t sel, uint8_t active, uint8_t mode, uint8_t waveform, uint16_t low, uint16_t high, uint16_t period)
{
    Ui_LineBegin();
    Ui_PutStr(item == sel ? "> " : "  ");
    Ui_PutSignalItem(item, active, mode, waveform, low, high, period);
    Ui_LineFlush(page);
}

void Ui_DrawBootStep(uint8_t step)
{
    switch(step)
    {
    case 0:
        Ui_WriteText(1, "   AOMaster");
        break;
    case 1:
        Ui_WriteText(3, "  SSD1306 OK");
        break;
    case 2:
        Ui_WriteText(5, "Starting..");
        break;
    case 3:
        Ui_WriteText(7, "GP8630 ...");
        break;
    default:
        break;
    }
}

void Ui_DrawHome(int16_t permille, GP8630_OutputMode_t mode, uint8_t gp_ok, uint16_t step_permille, uint8_t full_redraw)
{
    static int16_t old_permille;
    static uint16_t old_step;
    static uint8_t old_mode;
    static uint8_t old_gp;
    static uint8_t old_bar;
    static uint8_t valid;
    uint8_t mode_u8 = (uint8_t)mode;
    uint8_t bar = Ui_BarFill(permille);

    if(full_redraw)
        valid = 0;

    if(!valid || old_gp != gp_ok)
    {
        Ui_LineBegin();
        Ui_PutStr("AOMaster ");
        Ui_PutStr(gp_ok ? "OK" : "N/A");
        Ui_LineFlush(0);
    }

    if(!valid || old_mode != mode_u8)
    {
        Ui_LineBegin();
        Ui_PutStr("Mode:");
        Ui_PutMode(mode);
        Ui_LineFlush(1);
        Ui_WriteBlank(5);
        Ui_WriteText(7, "OK=Step L=Menu");
    }

    if(!valid || old_permille != permille || old_mode != mode_u8)
    {
        Ui_LineBegin();
        Ui_PutStr("Val:");
        Ui_PutValue(permille, mode);
        Ui_LineFlush(2);

        Ui_LineBegin();
        Ui_PutStr("Pct:");
        Ui_PutPercent(permille);
        Ui_LineFlush(3);
    }

    if(!valid || old_bar != bar)
    {
        Ui_LineBegin();
        Ui_PutStr("Bar:");
        Ui_PutBar(bar);
        Ui_LineFlush(4);
    }

    if(!valid || old_step != step_permille || old_mode != mode_u8)
    {
        Ui_LineBegin();
        Ui_PutStr("Step:");
        Ui_PutStepValue(step_permille, mode);
        Ui_LineFlush(6);
    }

    old_permille = permille;
    old_step = step_permille;
    old_mode = mode_u8;
    old_gp = gp_ok;
    old_bar = bar;
    valid = 1;
}

void Ui_DrawMainMenu(uint8_t sel)
{
    Ui_WriteText(0, "       MENU");
    Ui_WriteMenuItem(1, sel == 0U, "Output Mode");
    Ui_WriteMenuItem(2, sel == 1U, "Signal Gen");
    Ui_WriteMenuItem(3, sel == 2U, "Calibrate");
    Ui_WriteMenuItem(4, sel == 3U, "Brightness");
    Ui_WriteMenuItem(5, sel == 4U, "Status");
    Ui_WriteMenuItem(6, sel == 5U, "Exit");
    Ui_WriteText(7, "OK=Enter L=Back");
}

void Ui_DrawModeMenu(uint8_t sel)
{
    Ui_WriteText(0, "       MODE");
    Ui_WriteBlank(1);
    Ui_WriteMenuItem(2, sel == 0U, "0-10V Volt");
    Ui_WriteBlank(3);
    Ui_WriteMenuItem(4, sel == 1U, "4-20mA Cur");
    Ui_WriteBlank(5);
    Ui_WriteBlank(6);
    Ui_WriteText(7, "OK=Save L=Back");
}

void Ui_DrawCalModeMenu(uint8_t sel)
{
    Ui_WriteText(0, "        CAL");
    Ui_WriteBlank(1);
    Ui_WriteMenuItem(2, sel == 0U, "0V / 10V");
    Ui_WriteBlank(3);
    Ui_WriteMenuItem(4, sel == 1U, "4mA / 20mA");
    Ui_WriteBlank(5);
    Ui_WriteBlank(6);
    Ui_WriteText(7, "OK=Enter L=Back");
}

void Ui_DrawCalAdjust(GP8630_OutputMode_t mode, uint8_t full_step, uint16_t code)
{
    Ui_LineBegin();
    Ui_PutStr("CAL ");
    Ui_PutMode(mode);
    Ui_LineFlush(0);

    if(mode == GP8630_OUT_CURR_20MA)
        Ui_WriteText(1, full_step ? "HIGH 20mA" : "LOW 4mA");
    else
        Ui_WriteText(1, full_step ? "HIGH 10V" : "LOW 0V");

    Ui_WriteBlank(2);
    Ui_LineBegin();
    Ui_PutStr("Code:");
    Ui_PutCode(code);
    Ui_LineFlush(3);
    Ui_WriteBlank(4);
    Ui_WriteText(5, "Turn=Adjust");
    Ui_WriteBlank(6);
    Ui_WriteText(7, "OK=Next L=Back");
}

void Ui_DrawCalSave(GP8630_OutputMode_t mode, uint16_t zero_code, uint16_t full_code, uint8_t err)
{
    Ui_LineBegin();
    Ui_PutStr("Save ");
    Ui_PutMode(mode);
    Ui_LineFlush(0);

    Ui_WriteBlank(1);
    Ui_LineBegin();
    Ui_PutStr("Z:");
    Ui_PutCode(zero_code);
    Ui_PutStr(" F:");
    Ui_PutCode(full_code);
    Ui_LineFlush(2);
    Ui_WriteBlank(3);
    Ui_WriteBlank(4);
    Ui_WriteText(5, err ? "Range ERR" : "Save Cal?");
    Ui_WriteBlank(6);
    Ui_WriteText(7, "OK=Save L=Back");
}

void Ui_DrawBrightness(uint8_t brightness)
{
    uint8_t fill;

    Ui_WriteText(0, "   BRIGHTNESS");
    Ui_WriteBlank(1);

    Ui_LineBegin();
    Ui_PutStr("Level:");
    Ui_PutDec(brightness);
    Ui_PutStr("/255");
    Ui_LineFlush(2);

    Ui_WriteBlank(3);

    fill = (brightness + 12U) / 26U;
    if(fill > 10U) fill = 10U;
    Ui_LineBegin();
    Ui_PutBar(fill);
    Ui_LineFlush(4);

    Ui_WriteBlank(5);
    Ui_WriteBlank(6);
    Ui_WriteText(7, "OK=Save L=Back");
}

void Ui_DrawStatus(const char *gp, uint8_t addr, uint32_t i2c_err)
{
    Ui_WriteText(0, "      STATUS");
    Ui_WriteBlank(1);

    Ui_LineBegin();
    Ui_PutStr("GP :");
    Ui_PutStr(gp);
    Ui_LineFlush(2);

    Ui_LineBegin();
    Ui_PutStr("ADR:0x");
    Ui_PutHex8(addr);
    Ui_LineFlush(3);

    Ui_LineBegin();
    Ui_PutStr("I2C ERR:");
    Ui_PutDec(i2c_err);
    Ui_LineFlush(4);
    Ui_WriteBlank(5);
    Ui_WriteBlank(6);
    Ui_WriteText(7, "L=Back");
}

void Ui_DrawSignalMenu(uint8_t sel, uint8_t editing, uint8_t active, uint8_t mode, uint8_t waveform, uint16_t low, uint16_t high, uint16_t period)
{
    uint8_t i;

    Ui_WriteText(0, "    SIGNAL GEN");
    for(i = 0U; i < 6U; i++)
        Ui_WriteSignalItem((uint8_t)(i + 1U), i, sel, active, mode, waveform, low, high, period);
    Ui_WriteText(7, editing ? "Turn=Adj OK=Done" : "OK=Edit L=Back");
}

void Ui_DrawMessage(const char *line1, const char *line3)
{
    Ui_WriteBlank(0);
    Ui_WriteText(1, line1);
    Ui_WriteBlank(2);
    Ui_WriteText(3, line3);
    Ui_WriteBlank(4);
    Ui_WriteBlank(5);
    Ui_WriteBlank(6);
    Ui_WriteBlank(7);
}

