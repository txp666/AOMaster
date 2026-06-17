#include "App/settings.h"
#include "Sys/log.h"
#include "Sys/system.h"
#include <ch32v00x_flash.h>
#include <stddef.h>

#define SETTINGS_FLASH_ADDR     0x08003FC0U
#define SETTINGS_MAGIC          0xA05E0006U
#define SETTINGS_SAVE_DELAY_MS  500U
#define SETTINGS_CAL_MIN_SPAN   1000U
#define SETTINGS_FLASH_WORDS    16U
#define SETTINGS_VOLT_10V_CODE_12V   54613U
#define SETTINGS_CURR_4MA_CODE_24MA  10923U
#define SETTINGS_CURR_20MA_CODE_24MA 54613U

typedef struct
{
    uint32_t magic;
    int16_t permille;
    uint8_t mode;
    uint8_t brightness;
    SettingsCal_t cal[2];
    uint8_t csum;
    uint8_t pad[3];
} SettingsRaw_t;

static Settings_t s_cfg;
static uint8_t s_dirty;
static uint32_t s_dirty_tick;

static uint8_t Settings_Checksum(const SettingsRaw_t *raw)
{
    const uint8_t *p = (const uint8_t *)raw;
    uint8_t sum = 0;
    uint8_t i;

    for(i = 0; i < offsetof(SettingsRaw_t, csum); i++)
        sum ^= p[i];
    return sum;
}

uint8_t Settings_ModeToCalIndex(GP8630_OutputMode_t mode)
{
    return (mode == GP8630_OUT_CURR_20MA) ? 1U : 0U;
}

static uint8_t Settings_IsSupportedMode(GP8630_OutputMode_t mode)
{
    return (mode == GP8630_OUT_VOLT_10V || mode == GP8630_OUT_CURR_20MA) ? 1U : 0U;
}

int16_t Settings_GetPermilleMin(GP8630_OutputMode_t mode)
{
    return (mode == GP8630_OUT_CURR_20MA) ? SETTINGS_PERMILLE_CURR_MIN : SETTINGS_PERMILLE_VOLT_MIN;
}

int16_t Settings_GetPermilleMax(GP8630_OutputMode_t mode)
{
    return (mode == GP8630_OUT_CURR_20MA) ? SETTINGS_PERMILLE_CURR_MAX : SETTINGS_PERMILLE_VOLT_MAX;
}

static int16_t Settings_ClampPermille(GP8630_OutputMode_t mode, int16_t permille)
{
    int16_t min = Settings_GetPermilleMin(mode);
    int16_t max = Settings_GetPermilleMax(mode);

    if(permille < min)
        return min;
    if(permille > max)
        return max;
    return permille;
}

static void Settings_SetDefaultCal(SettingsCal_t *cal, uint8_t index)
{
    if(index == 1U)
    {
        cal->zero_code = SETTINGS_CURR_4MA_CODE_24MA;
        cal->full_code = SETTINGS_CURR_20MA_CODE_24MA;
    }
    else
    {
        cal->zero_code = 0U;
        cal->full_code = SETTINGS_VOLT_10V_CODE_12V;
    }
}

static void Settings_Default(void)
{
    s_cfg.output_mode = GP8630_OUT_VOLT_10V;
    s_cfg.permille = 0;
    s_cfg.brightness = SETTINGS_BRIGHTNESS_DEFAULT;
    Settings_SetDefaultCal(&s_cfg.cal[0], 0);
    Settings_SetDefaultCal(&s_cfg.cal[1], 1);
}

static uint8_t Settings_CalValid(const SettingsCal_t *cal)
{
    return ((uint32_t)cal->full_code > (uint32_t)cal->zero_code + SETTINGS_CAL_MIN_SPAN) ? 1U : 0U;
}

static void Settings_Load(void)
{
    const SettingsRaw_t *raw = (const SettingsRaw_t *)SETTINGS_FLASH_ADDR;

    if(raw->magic != SETTINGS_MAGIC || raw->csum != Settings_Checksum(raw))
    {
        Settings_Default();
        LOGW("settings default\r\n");
        return;
    }

    if(!Settings_IsSupportedMode((GP8630_OutputMode_t)raw->mode) ||
       raw->permille < Settings_GetPermilleMin((GP8630_OutputMode_t)raw->mode) ||
       raw->permille > Settings_GetPermilleMax((GP8630_OutputMode_t)raw->mode) ||
       !Settings_CalValid(&raw->cal[0]) || !Settings_CalValid(&raw->cal[1]))
    {
        Settings_Default();
        LOGW("settings invalid\r\n");
        return;
    }

    s_cfg.output_mode = (GP8630_OutputMode_t)raw->mode;
    s_cfg.permille = raw->permille;
    s_cfg.brightness = raw->brightness;
    if(s_cfg.brightness < SETTINGS_BRIGHTNESS_MIN || s_cfg.brightness > SETTINGS_BRIGHTNESS_MAX)
        s_cfg.brightness = SETTINGS_BRIGHTNESS_DEFAULT;
    s_cfg.cal[0] = raw->cal[0];
    s_cfg.cal[1] = raw->cal[1];
    LOGI("settings load mode=%u permille=%d brightness=%u\r\n",
         (unsigned)s_cfg.output_mode, (int)s_cfg.permille, (unsigned)s_cfg.brightness);
}

void Settings_Init(void)
{
    Settings_Load();
}

const Settings_t *Settings_Get(void)
{
    return &s_cfg;
}

void Settings_SetMode(GP8630_OutputMode_t mode)
{
    if(Settings_IsSupportedMode(mode))
    {
        s_cfg.output_mode = mode;
        s_cfg.permille = Settings_ClampPermille(s_cfg.output_mode, s_cfg.permille);
    }
}

void Settings_SetPermille(int16_t permille)
{
    s_cfg.permille = Settings_ClampPermille(s_cfg.output_mode, permille);
}

const SettingsCal_t *Settings_GetCal(GP8630_OutputMode_t mode)
{
    return &s_cfg.cal[Settings_ModeToCalIndex(mode)];
}

uint8_t Settings_IsCalibrated(GP8630_OutputMode_t mode)
{
    const SettingsCal_t *cal = Settings_GetCal(mode);
    SettingsCal_t def;
    uint8_t idx = Settings_ModeToCalIndex(mode);

    Settings_SetDefaultCal(&def, idx);
    return (cal->zero_code != def.zero_code || cal->full_code != def.full_code) ? 1U : 0U;
}

uint8_t Settings_SetCal(GP8630_OutputMode_t mode, uint16_t zero_code, uint16_t full_code)
{
    SettingsCal_t *cal;

    if(!Settings_IsSupportedMode(mode))
        return 1;
    if((uint32_t)full_code <= (uint32_t)zero_code + SETTINGS_CAL_MIN_SPAN)
        return 1;

    cal = &s_cfg.cal[Settings_ModeToCalIndex(mode)];
    cal->zero_code = zero_code;
    cal->full_code = full_code;
    return 0;
}

uint8_t Settings_GetBrightness(void)
{
    return s_cfg.brightness;
}

void Settings_SetBrightness(uint8_t brightness)
{
    if(brightness >= SETTINGS_BRIGHTNESS_MIN && brightness <= SETTINGS_BRIGHTNESS_MAX)
        s_cfg.brightness = brightness;
}

uint8_t Settings_Save(void)
{
    SettingsRaw_t raw;
    uint32_t page[SETTINGS_FLASH_WORDS];
    uint8_t *dst = (uint8_t *)page;
    const uint8_t *src = (const uint8_t *)&raw;
    uint8_t i;

    raw.magic = SETTINGS_MAGIC;
    raw.permille = s_cfg.permille;
    raw.mode = (uint8_t)s_cfg.output_mode;
    raw.brightness = s_cfg.brightness;
    raw.cal[0] = s_cfg.cal[0];
    raw.cal[1] = s_cfg.cal[1];
    raw.csum = Settings_Checksum(&raw);
    raw.pad[0] = 0;
    raw.pad[1] = 0;
    raw.pad[2] = 0;

    for(i = 0; i < SETTINGS_FLASH_WORDS; i++)
        page[i] = 0xFFFFFFFFU;

    for(i = 0; i < sizeof(SettingsRaw_t); i++)
        dst[i] = src[i];

    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast(SETTINGS_FLASH_ADDR);
    FLASH_BufReset();
    for(i = 0; i < SETTINGS_FLASH_WORDS; i++)
        FLASH_BufLoad(SETTINGS_FLASH_ADDR + ((uint32_t)i * 4U), page[i]);
    FLASH_ProgramPage_Fast(SETTINGS_FLASH_ADDR);
    FLASH_Lock_Fast();
    FLASH_Lock();

    s_dirty = 0;
    LOGI("settings saved\r\n");
    return 0;
}

void Settings_RequestSave(void)
{
    s_dirty = 1;
    s_dirty_tick = System_GetTick();
}

void Settings_FlushPending(void)
{
    if(!s_dirty)
        return;

    if((System_GetTick() - s_dirty_tick) < SETTINGS_SAVE_DELAY_MS)
        return;

    Settings_Save();
}
