#ifndef __SETTINGS_H
#define __SETTINGS_H

#include <stdint.h>
#include "Drv/gp8630.h"

#define SETTINGS_PERMILLE_SCALE     8
#define SETTINGS_PERMILLE_BASE      (1000 * SETTINGS_PERMILLE_SCALE)
#define SETTINGS_PERMILLE_VOLT_MIN  (0 * SETTINGS_PERMILLE_SCALE)
#define SETTINGS_PERMILLE_VOLT_MAX  (1200 * SETTINGS_PERMILLE_SCALE)
#define SETTINGS_PERMILLE_CURR_MIN  (-250 * SETTINGS_PERMILLE_SCALE)
#define SETTINGS_PERMILLE_CURR_MAX  (1250 * SETTINGS_PERMILLE_SCALE)

#define SETTINGS_BRIGHTNESS_DEFAULT 0xCF
#define SETTINGS_BRIGHTNESS_MIN     0x01
#define SETTINGS_BRIGHTNESS_MAX     0xFF

typedef struct
{
    uint16_t zero_code;
    uint16_t full_code;
} SettingsCal_t;

typedef struct
{
    GP8630_OutputMode_t output_mode;
    int16_t permille;
    SettingsCal_t cal[2];
    uint8_t brightness;
} Settings_t;

void Settings_Init(void);
const Settings_t *Settings_Get(void);
void Settings_SetMode(GP8630_OutputMode_t mode);
void Settings_SetPermille(int16_t permille);
int16_t Settings_GetPermilleMin(GP8630_OutputMode_t mode);
int16_t Settings_GetPermilleMax(GP8630_OutputMode_t mode);
uint8_t Settings_ModeToCalIndex(GP8630_OutputMode_t mode);
const SettingsCal_t *Settings_GetCal(GP8630_OutputMode_t mode);
uint8_t Settings_IsCalibrated(GP8630_OutputMode_t mode);
uint8_t Settings_SetCal(GP8630_OutputMode_t mode, uint16_t zero_code, uint16_t full_code);
uint8_t Settings_GetBrightness(void);
void Settings_SetBrightness(uint8_t brightness);
uint8_t Settings_Save(void);
void Settings_RequestSave(void);
void Settings_FlushPending(void);

#endif
