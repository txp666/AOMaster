#ifndef __LOG_H
#define __LOG_H

#ifndef AOMASTER_LOG_ENABLE
#define AOMASTER_LOG_ENABLE 0
#endif

#if AOMASTER_LOG_ENABLE
#include "debug.h"
#include "Sys/system.h"
#define LOGI(fmt, ...)  printf("[%lu] I: " fmt, (unsigned long)System_GetTick(), ##__VA_ARGS__)
#define LOGW(fmt, ...)  printf("[%lu] W: " fmt, (unsigned long)System_GetTick(), ##__VA_ARGS__)
#define LOGE(fmt, ...)  printf("[%lu] E: " fmt, (unsigned long)System_GetTick(), ##__VA_ARGS__)
#else
#define LOGI(fmt, ...)  do { } while(0)
#define LOGW(fmt, ...)  do { } while(0)
#define LOGE(fmt, ...)  do { } while(0)
#endif

#endif
