/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Description        : AOMaster entry point.
 *                      I2C OLED: SCL=PC2 SDA=PC1
 *                      ENC: A=PD4 B=PD2 KEY=PD3
 *                      BUZZ: PC0
 *                      GP8630: I2C 0x58 (shared with OLED)
 *********************************************************************************/

#include "Sys/system.h"

int main(void)
{
    System_Init();
    System_Run();
}
