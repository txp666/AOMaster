#ifndef __UI_H
#define __UI_H

#include <stdint.h>
#include "Drv/gp8630.h"

void Ui_DrawBootStep(uint8_t step);
void Ui_DrawHome(int16_t permille, GP8630_OutputMode_t mode, uint8_t gp_ok, uint16_t step_permille, uint8_t full_redraw);
void Ui_DrawMainMenu(uint8_t sel);
void Ui_DrawModeMenu(uint8_t sel);
void Ui_DrawCalModeMenu(uint8_t sel);
void Ui_DrawCalAdjust(GP8630_OutputMode_t mode, uint8_t full_step, uint16_t code);
void Ui_DrawCalSave(GP8630_OutputMode_t mode, uint16_t zero_code, uint16_t full_code, uint8_t err);
void Ui_DrawBrightness(uint8_t brightness);
void Ui_DrawStatus(const char *gp, uint8_t addr, uint32_t i2c_err);
void Ui_DrawSignalMenu(uint8_t sel, uint8_t editing, uint8_t active, uint8_t mode, uint8_t waveform, uint16_t low, uint16_t high, uint16_t period);
void Ui_DrawMessage(const char *line1, const char *line3);

#endif
