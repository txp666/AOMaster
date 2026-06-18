#include "App/app.h"
#include "App/settings.h"
#include "App/ui.h"
#include "Drv/gp8630.h"
#include "Drv/ssd1306.h"
#include "Bsp/encoder.h"
#include "Bsp/i2c_bus.h"
#include "Sys/power.h"
#include "Sys/log.h"
#include "Sys/system.h"
#include "ch32v00x.h"

#define APP_BOOT_DELAY_MS       100U
#define APP_BOOT_DRAW_TICKS     32U
#define APP_VOLT_FINE_STEP      (1U * SETTINGS_PERMILLE_SCALE)
#define APP_VOLT_NORM_STEP      (10U * SETTINGS_PERMILLE_SCALE)
#define APP_VOLT_FAST_STEP      (100U * SETTINGS_PERMILLE_SCALE)
#define APP_CURR_FINE_STEP      5U
#define APP_CURR_NORM_STEP      50U
#define APP_CURR_FAST_STEP      500U
#define APP_CODE_STEP           1
#define APP_BRIGHTNESS_STEP     8
#define APP_HOME_DRAW_MIN_MS    80U
#define APP_MAIN_MENU_COUNT     6U
#define APP_SIGNAL_MENU_COUNT   6U
#define APP_SIGNAL_VALUE_STEP   100U
#define APP_SIGNAL_PERIOD_STEP  100U
#define APP_SIGNAL_PERIOD_MIN   100U
#define APP_SIGNAL_PERIOD_MAX   60000U

#define APP_STEP_LEVEL_NORM     0U
#define APP_STEP_LEVEL_FAST     1U
#define APP_STEP_LEVEL_FINE     2U
#define APP_STEP_LEVEL_COUNT    3U

#define AOM_SLAVE_ID            1U
#define AOM_RX_MAX              64U
#define AOM_TX_MAX              64U
#define AOM_REG_COUNT           23U
#define AOM_STEP_MAX            16U
#define AOM_RX_TIMEOUT_MS       50U
#define AOM_WAVE_MIN_PERIOD_MS  1U
#define AOM_WAVE_UPDATE_MS      20U

#define AOM_REG_SIGNAL_TYPE     0U
#define AOM_REG_WAVEFORM        1U
#define AOM_REG_VALUE_A         2U
#define AOM_REG_VALUE_B         3U
#define AOM_REG_PERIOD_MS       4U
#define AOM_REG_DUTY            5U
#define AOM_REG_ACTUAL          6U
#define AOM_REG_STEP_START      7U

#define MODBUS_FC_READ_HOLDING  3U
#define MODBUS_FC_WRITE_SINGLE  6U
#define MODBUS_FC_WRITE_MULTI   16U

typedef enum
{
    AOM_MODE_CURRENT = 0,
    AOM_MODE_VOLTAGE = 1,
} AomMode_t;

typedef enum
{
    AOM_WAVE_CONSTANT = 0,
    AOM_WAVE_STEP = 1,
    AOM_WAVE_RAMP = 2,
    AOM_WAVE_SQUARE = 3,
    AOM_WAVE_TRIANGLE = 4,
    AOM_WAVE_SINE = 5,
} AomWaveform_t;

typedef enum
{
    VIEW_HOME = 0,
    VIEW_MAIN_MENU,
    VIEW_MODE_MENU,
    VIEW_CAL_MODE,
    VIEW_CAL_WAIT,
    VIEW_CAL_ZERO,
    VIEW_CAL_FULL,
    VIEW_CAL_SAVE,
    VIEW_STATUS,
    VIEW_BRIGHTNESS,
    VIEW_SIGNAL_MENU,
    VIEW_SIGNAL_EDIT,
} AppView_t;

typedef struct
{
    AppState_t state;
    AppView_t view;
    uint32_t state_tick;
    uint8_t draw_step;
    uint8_t gp_reconfig;
    uint8_t step_level;
    uint8_t menu_sel;
    uint8_t mode_sel;
    uint8_t cal_sel;
    uint8_t cal_err;
    uint8_t signal_sel;
    GP8630_OutputMode_t cal_mode;
    uint16_t cal_zero;
    uint16_t cal_full;
    uint8_t brightness;
} AppCtx_t;

typedef struct
{
    int16_t permille;
    uint16_t step;
    uint8_t mode;
    uint8_t gp;
    uint8_t valid;
} AppUiCache_t;

typedef struct
{
    uint16_t regs[AOM_REG_COUNT];
    uint16_t actual;
    uint16_t last_value;
    uint16_t step_index;
    uint16_t step_loop;
    uint32_t start_tick;
    uint32_t last_apply_tick;
    uint8_t remote_active;
} AomSignalCtx_t;

static const GP8630_OutputMode_t s_modes[2] =
{
    GP8630_OUT_VOLT_10V,
    GP8630_OUT_CURR_20MA,
};

static const uint8_t s_local_waves[4] =
{
    AOM_WAVE_RAMP,
    AOM_WAVE_SQUARE,
    AOM_WAVE_TRIANGLE,
    AOM_WAVE_SINE,
};

static AppCtx_t s_app;
static AppUiCache_t s_ui;
static AomSignalCtx_t s_sig;
static uint8_t s_rx[AOM_RX_MAX];
static volatile uint8_t s_rx_len;
static volatile uint32_t s_rx_tick;
static uint32_t s_ui_tick;

static void Signal_Init(void);
static void Signal_Update(void);
static void Signal_DisableRemote(void);
static uint8_t Signal_IsRemoteActive(void);
static void Signal_ApplyCurrent(void);
static void Signal_SetLocalFromSettings(void);
static void Signal_NormalizeConfig(void);
static void Signal_ConfigChanged(void);

static void App_InvalidateUi(void)
{
    s_ui.valid = 0;
}

static uint16_t App_GetPermilleStep(GP8630_OutputMode_t mode)
{
    if(mode == GP8630_OUT_CURR_20MA)
    {
        switch(s_app.step_level)
        {
        case APP_STEP_LEVEL_FINE:
            return APP_CURR_FINE_STEP;
        case APP_STEP_LEVEL_FAST:
            return APP_CURR_FAST_STEP;
        default:
            return APP_CURR_NORM_STEP;
        }
    }

    switch(s_app.step_level)
    {
    case APP_STEP_LEVEL_FINE:
        return APP_VOLT_FINE_STEP;
    case APP_STEP_LEVEL_FAST:
        return APP_VOLT_FAST_STEP;
    default:
        return APP_VOLT_NORM_STEP;
    }
}

static uint8_t App_ModeToIndex(GP8630_OutputMode_t mode)
{
    return (mode == GP8630_OUT_CURR_20MA) ? 1U : 0U;
}

static uint8_t App_AdjustSel(uint8_t sel, int16_t diff, uint8_t count)
{
    if(diff > 0)
    {
        if(sel + 1U < count)
            sel++;
    }
    else if(diff < 0)
    {
        if(sel > 0U)
            sel--;
    }
    return sel;
}

static uint16_t App_AdjustCode(uint16_t code, int16_t diff)
{
    int32_t v = (int32_t)code + (int32_t)diff * APP_CODE_STEP;

    if(v < 0)
        return 0;
    if(v > (int32_t)GP8630_CODE_MAX)
        return GP8630_CODE_MAX;
    return (uint16_t)v;
}

static uint16_t App_AdjustU16(uint16_t value, int16_t diff, uint16_t step, uint16_t min, uint16_t max)
{
    int32_t v = (int32_t)value + (int32_t)diff * step;

    if(v < (int32_t)min)
        return min;
    if(v > (int32_t)max)
        return max;
    return (uint16_t)v;
}

static uint8_t App_AdjustLocalWave(uint8_t waveform, int16_t diff)
{
    uint8_t i;

    if(diff > 0)
    {
        for(i = 0U; i < sizeof(s_local_waves); i++)
        {
            if(s_local_waves[i] == waveform && i + 1U < sizeof(s_local_waves))
                return s_local_waves[i + 1U];
        }
        return s_local_waves[0];
    }

    if(diff < 0)
    {
        for(i = (uint8_t)sizeof(s_local_waves); i > 0U; i--)
        {
            if(s_local_waves[i - 1U] == waveform && i > 1U)
                return s_local_waves[i - 2U];
        }
        return s_local_waves[sizeof(s_local_waves) - 1U];
    }

    return waveform;
}

static void App_EnsureLocalSignalWave(void)
{
    if(s_sig.regs[AOM_REG_WAVEFORM] == AOM_WAVE_CONSTANT)
        s_sig.regs[AOM_REG_WAVEFORM] = AOM_WAVE_RAMP;
}

static uint16_t App_OutputCode(GP8630_OutputMode_t mode, int16_t permille)
{
    const SettingsCal_t *cal = Settings_GetCal(mode);
    uint32_t span;
    int32_t code;

    if(cal->full_code <= cal->zero_code)
        return 0;

    span = (uint32_t)cal->full_code - cal->zero_code;
    if(mode == GP8630_OUT_CURR_20MA)
        code = (int32_t)cal->zero_code + ((int32_t)span * permille) / SETTINGS_PERMILLE_BASE;
    else
        code = (int32_t)cal->zero_code +
               ((int32_t)span * ((int32_t)permille - (SETTINGS_PERMILLE_BASE / 10L))) /
               (SETTINGS_PERMILLE_BASE - (SETTINGS_PERMILLE_BASE / 10L));
    if(code < 0)
        return 0;
    if(code > (int32_t)GP8630_CODE_MAX)
        code = GP8630_CODE_MAX;
    return (uint16_t)code;
}

static void App_ApplyOutput(void)
{
    const Settings_t *cfg = Settings_Get();

    if(!GP8630_IsReady())
        return;

    GP8630_RequestCode(App_OutputCode(cfg->output_mode, cfg->permille));
    Signal_SetLocalFromSettings();
}

static void App_DrawHomeIfDirty(void)
{
    const Settings_t *cfg = Settings_Get();
    uint8_t gp = GP8630_IsReady();
    uint16_t step = App_GetPermilleStep(cfg->output_mode);
    uint8_t full_redraw;
    uint8_t value_only;

    if(s_app.view != VIEW_HOME)
        return;

    if(s_ui.valid &&
       s_ui.permille == cfg->permille &&
       s_ui.step == step &&
       s_ui.mode == (uint8_t)cfg->output_mode &&
       s_ui.gp == gp)
        return;

    full_redraw = (!s_ui.valid ||
                   s_ui.mode != (uint8_t)cfg->output_mode ||
                   s_ui.gp != gp);
    value_only = (s_ui.valid &&
                  !full_redraw &&
                  s_ui.step == step &&
                  s_ui.mode == (uint8_t)cfg->output_mode &&
                  s_ui.gp == gp);
    if(value_only && (System_GetTick() - s_ui_tick) < APP_HOME_DRAW_MIN_MS)
        return;

    Ui_DrawHome(cfg->permille, cfg->output_mode, gp, step, full_redraw);
    s_ui.permille = cfg->permille;
    s_ui.step = step;
    s_ui.mode = (uint8_t)cfg->output_mode;
    s_ui.gp = gp;
    s_ui.valid = 1;
    s_ui_tick = System_GetTick();
}

static uint16_t Signal_ModeMinRaw(uint8_t mode)
{
    return (mode == AOM_MODE_CURRENT) ? 4000U : 0U;
}

static uint16_t Signal_ModeMaxRaw(uint8_t mode)
{
    return (mode == AOM_MODE_CURRENT) ? 20000U : 10000U;
}

static uint16_t Signal_ClampRaw(uint8_t mode, uint16_t raw)
{
    uint16_t min = Signal_ModeMinRaw(mode);
    uint16_t max = Signal_ModeMaxRaw(mode);

    if(raw < min)
        return min;
    if(raw > max)
        return max;
    return raw;
}

static uint8_t Signal_IsVoltageMode(uint8_t mode)
{
    return (mode == AOM_MODE_VOLTAGE) ? 1U : 0U;
}

static GP8630_OutputMode_t Signal_ToGpMode(uint8_t mode)
{
    return Signal_IsVoltageMode(mode) ? GP8630_OUT_VOLT_10V : GP8630_OUT_CURR_20MA;
}

static int16_t Signal_RawToPermille(uint8_t mode, uint16_t raw)
{
    GP8630_OutputMode_t gp_mode = Signal_ToGpMode(mode);
    int32_t permille;
    int16_t min;
    int16_t max;

    if(Signal_IsVoltageMode(mode))
        permille = ((int32_t)raw * SETTINGS_PERMILLE_BASE) / 10000L;
    else
        permille = (((int32_t)raw - 4000L) * SETTINGS_PERMILLE_BASE) / 16000L;

    min = Settings_GetPermilleMin(gp_mode);
    max = Settings_GetPermilleMax(gp_mode);
    if(permille < min)
        permille = min;
    if(permille > max)
        permille = max;
    return (int16_t)permille;
}

static uint16_t Signal_CodeFromRaw(uint8_t mode, uint16_t raw)
{
    raw = Signal_ClampRaw(mode, raw);
    return App_OutputCode(Signal_ToGpMode(mode), Signal_RawToPermille(mode, raw));
}

static uint16_t Signal_SettingsRaw(void)
{
    const Settings_t *cfg = Settings_Get();
    int32_t raw;

    if(cfg->output_mode == GP8630_OUT_CURR_20MA)
        raw = 4000L + ((int32_t)cfg->permille * 16000L) / SETTINGS_PERMILLE_BASE;
    else
        raw = ((int32_t)cfg->permille * 10000L) / SETTINGS_PERMILLE_BASE;

    if(raw < 0)
        raw = 0;
    if(raw > 65535L)
        raw = 65535L;
    return (uint16_t)raw;
}

static void Signal_SetLocalFromSettings(void)
{
    uint8_t mode = (Settings_Get()->output_mode == GP8630_OUT_CURR_20MA) ?
                   AOM_MODE_CURRENT : AOM_MODE_VOLTAGE;
    uint16_t raw;

    if(s_sig.remote_active)
        return;

    raw = Signal_SettingsRaw();
    s_sig.regs[AOM_REG_SIGNAL_TYPE] = mode;
    s_sig.regs[AOM_REG_WAVEFORM] = AOM_WAVE_CONSTANT;
    s_sig.regs[AOM_REG_VALUE_A] = raw;
    s_sig.regs[AOM_REG_VALUE_B] = raw;
    s_sig.actual = raw;
    s_sig.regs[AOM_REG_ACTUAL] = raw;
}

static void App_DrawSignalMenu(void)
{
    uint8_t waveform = (uint8_t)s_sig.regs[AOM_REG_WAVEFORM];

    if(!Signal_IsRemoteActive() && waveform == AOM_WAVE_CONSTANT)
        waveform = AOM_WAVE_RAMP;

    Ui_DrawSignalMenu(s_app.signal_sel,
                      s_app.view == VIEW_SIGNAL_EDIT,
                      Signal_IsRemoteActive(),
                      (uint8_t)s_sig.regs[AOM_REG_SIGNAL_TYPE],
                      waveform,
                      s_sig.regs[AOM_REG_VALUE_A],
                      s_sig.regs[AOM_REG_VALUE_B],
                      s_sig.regs[AOM_REG_PERIOD_MS]);
}

static void App_DrawSignalEdit(void)
{
    App_DrawSignalMenu();
}

static void Signal_DisableRemote(void)
{
    if(!s_sig.remote_active)
        return;

    s_sig.remote_active = 0;
    Signal_SetLocalFromSettings();
}

static uint8_t Signal_IsRemoteActive(void)
{
    return s_sig.remote_active;
}

static void Signal_ApplyRaw(uint16_t raw)
{
    uint8_t mode = (uint8_t)s_sig.regs[AOM_REG_SIGNAL_TYPE];
    GP8630_OutputMode_t gp_mode;

    if(mode > AOM_MODE_VOLTAGE)
        mode = AOM_MODE_CURRENT;

    raw = Signal_ClampRaw(mode, raw);
    gp_mode = Signal_ToGpMode(mode);
    s_sig.actual = raw;
    s_sig.last_value = raw;
    s_sig.regs[AOM_REG_ACTUAL] = raw;

    Settings_SetMode(gp_mode);
    Settings_SetPermille(Signal_RawToPermille(mode, raw));
    App_InvalidateUi();

    if(GP8630_GetMode() != gp_mode)
    {
        GP8630_BeginInitWithMode(gp_mode);
        s_app.gp_reconfig = 1;
        if(s_app.view == VIEW_HOME)
            Ui_DrawMessage("Remote mode", "Reconfig...");
        return;
    }

    if(!GP8630_IsReady())
        return;

    GP8630_RequestCode(Signal_CodeFromRaw(mode, raw));
    if(s_app.view == VIEW_HOME)
        App_DrawHomeIfDirty();
}

static int16_t Signal_Sine1000(uint8_t index)
{
    static const uint16_t qsin[17] =
    {
        0U, 98U, 195U, 290U, 383U, 471U, 556U, 634U, 707U,
        773U, 831U, 882U, 924U, 957U, 981U, 995U, 1000U
    };
    uint8_t quad = (uint8_t)(index >> 4);
    uint8_t pos = (uint8_t)(index & 0x0FU);

    if(quad == 0U)
        return (int16_t)qsin[pos];
    if(quad == 1U)
        return (int16_t)qsin[16U - pos];
    if(quad == 2U)
        return -(int16_t)qsin[pos];

    return -(int16_t)qsin[16U - pos];
}

static uint16_t Signal_Evaluate(void)
{
    uint8_t mode = (uint8_t)s_sig.regs[AOM_REG_SIGNAL_TYPE];
    uint8_t waveform = (uint8_t)s_sig.regs[AOM_REG_WAVEFORM];
    uint16_t low = s_sig.regs[AOM_REG_VALUE_A];
    uint16_t high = s_sig.regs[AOM_REG_VALUE_B];
    uint16_t period = s_sig.regs[AOM_REG_PERIOD_MS];
    uint32_t elapsed = System_GetTick() - s_sig.start_tick;
    uint32_t phase;
    uint32_t span;
    uint16_t tmp;

    if(mode > AOM_MODE_VOLTAGE)
        mode = AOM_MODE_CURRENT;
    if(waveform > AOM_WAVE_SINE)
        waveform = AOM_WAVE_CONSTANT;

    if(waveform == AOM_WAVE_STEP)
    {
        uint16_t count = s_sig.regs[AOM_REG_VALUE_A];
        uint16_t dwell = s_sig.regs[AOM_REG_VALUE_B];
        uint16_t loops = s_sig.regs[AOM_REG_PERIOD_MS];
        uint32_t step;

        if(count == 0U)
            count = 1U;
        if(count > AOM_STEP_MAX)
            count = AOM_STEP_MAX;
        if(dwell == 0U)
            dwell = 1U;

        step = elapsed / dwell;
        if(loops != 0U && step >= ((uint32_t)count * loops))
            s_sig.step_index = (uint16_t)(count - 1U);
        else
            s_sig.step_index = (uint16_t)(step % count);

        return Signal_ClampRaw(mode, s_sig.regs[AOM_REG_STEP_START + s_sig.step_index]);
    }

    low = Signal_ClampRaw(mode, low);
    high = Signal_ClampRaw(mode, high);
    if(high < low)
    {
        tmp = low;
        low = high;
        high = tmp;
    }

    if(waveform == AOM_WAVE_CONSTANT)
        return low;

    if(period < AOM_WAVE_MIN_PERIOD_MS)
        period = AOM_WAVE_MIN_PERIOD_MS;

    phase = elapsed % period;
    span = (uint32_t)high - low;

    if(waveform == AOM_WAVE_RAMP)
        return (uint16_t)(low + (span * phase) / period);

    if(waveform == AOM_WAVE_SQUARE)
    {
        uint16_t duty = s_sig.regs[AOM_REG_DUTY];

        if(duty < 10U)
            duty = 10U;
        if(duty > 990U)
            duty = 990U;
        return (phase < ((uint32_t)period * duty) / 1000U) ? high : low;
    }

    if(waveform == AOM_WAVE_TRIANGLE)
    {
        uint32_t half = period / 2U;

        if(half == 0U)
            half = 1U;
        if(phase <= half)
            return (uint16_t)(low + (span * phase) / half);
        return (uint16_t)(high - (span * (phase - half)) / (period - half));
    }

    {
        uint8_t idx = (uint8_t)((phase * 64U) / period);
        int16_t s = Signal_Sine1000(idx);
        int32_t mid = ((int32_t)low + high) / 2L;
        int32_t amp = ((int32_t)high - low) / 2L;
        int32_t value = mid + (amp * s) / 1000L;

        if(value < low)
            value = low;
        if(value > high)
            value = high;
        return (uint16_t)value;
    }
}

static void Signal_ApplyCurrent(void)
{
    Signal_ApplyRaw(Signal_Evaluate());
    s_sig.last_apply_tick = System_GetTick();
}

static void Signal_NormalizeConfig(void)
{
    uint8_t mode = (uint8_t)s_sig.regs[AOM_REG_SIGNAL_TYPE];
    uint8_t waveform = (uint8_t)s_sig.regs[AOM_REG_WAVEFORM];
    uint8_t i;

    if(mode > AOM_MODE_VOLTAGE)
        mode = AOM_MODE_CURRENT;
    if(waveform > AOM_WAVE_SINE)
        waveform = AOM_WAVE_CONSTANT;
    s_sig.regs[AOM_REG_SIGNAL_TYPE] = mode;
    s_sig.regs[AOM_REG_WAVEFORM] = waveform;

    if(waveform == AOM_WAVE_STEP)
    {
        if(s_sig.regs[AOM_REG_VALUE_A] == 0U)
            s_sig.regs[AOM_REG_VALUE_A] = 1U;
        if(s_sig.regs[AOM_REG_VALUE_A] > AOM_STEP_MAX)
            s_sig.regs[AOM_REG_VALUE_A] = AOM_STEP_MAX;
        if(s_sig.regs[AOM_REG_VALUE_B] == 0U)
            s_sig.regs[AOM_REG_VALUE_B] = 1U;
    }
    else
    {
        s_sig.regs[AOM_REG_VALUE_A] = Signal_ClampRaw(mode, s_sig.regs[AOM_REG_VALUE_A]);
        s_sig.regs[AOM_REG_VALUE_B] = Signal_ClampRaw(mode, s_sig.regs[AOM_REG_VALUE_B]);
        if(s_sig.regs[AOM_REG_PERIOD_MS] == 0U && waveform != AOM_WAVE_CONSTANT)
            s_sig.regs[AOM_REG_PERIOD_MS] = 1000U;
        if(s_sig.regs[AOM_REG_DUTY] == 0U)
            s_sig.regs[AOM_REG_DUTY] = 500U;
    }

    for(i = 0U; i < AOM_STEP_MAX; i++)
        s_sig.regs[AOM_REG_STEP_START + i] = Signal_ClampRaw(mode, s_sig.regs[AOM_REG_STEP_START + i]);
}

static void Signal_ConfigChanged(void)
{
    Signal_NormalizeConfig();
    s_sig.remote_active = 1U;
    s_sig.step_index = 0U;
    s_sig.step_loop = 0U;
    s_sig.start_tick = System_GetTick();
    Signal_ApplyCurrent();
}

static void Signal_Update(void)
{
    uint16_t value;

    if(!s_sig.remote_active)
        return;

    if((System_GetTick() - s_sig.last_apply_tick) < AOM_WAVE_UPDATE_MS)
        return;

    value = Signal_Evaluate();
    if(value != s_sig.last_value || s_sig.regs[AOM_REG_WAVEFORM] != AOM_WAVE_CONSTANT)
        Signal_ApplyRaw(value);
    s_sig.last_apply_tick = System_GetTick();
}

static void Signal_Init(void)
{
    s_sig.regs[AOM_REG_SIGNAL_TYPE] = AOM_MODE_CURRENT;
    s_sig.regs[AOM_REG_WAVEFORM] = AOM_WAVE_CONSTANT;
    s_sig.regs[AOM_REG_VALUE_A] = 12000U;
    s_sig.regs[AOM_REG_VALUE_B] = 12000U;
    s_sig.regs[AOM_REG_PERIOD_MS] = 1000U;
    s_sig.regs[AOM_REG_DUTY] = 500U;
    s_sig.regs[AOM_REG_STEP_START + 0U] = 4000U;
    s_sig.regs[AOM_REG_STEP_START + 1U] = 8000U;
    s_sig.regs[AOM_REG_STEP_START + 2U] = 12000U;
    s_sig.regs[AOM_REG_STEP_START + 3U] = 16000U;
    s_sig.regs[AOM_REG_STEP_START + 4U] = 20000U;
    s_sig.remote_active = 0U;
    Signal_SetLocalFromSettings();
}

static uint16_t Modbus_Crc16(const uint8_t *data, uint8_t len)
{
    uint16_t crc = 0xFFFFU;
    uint8_t i;

    while(len--)
    {
        crc ^= *data++;
        for(i = 0U; i < 8U; i++)
            crc = (crc & 1U) ? (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
    }
    return crc;
}

static void Modbus_AppendCrc(uint8_t *buf, uint8_t len_without_crc)
{
    uint16_t crc = Modbus_Crc16(buf, len_without_crc);

    buf[len_without_crc] = (uint8_t)(crc & 0xFFU);
    buf[len_without_crc + 1U] = (uint8_t)(crc >> 8U);
}

static void Modbus_Send(uint8_t *buf, uint8_t len_without_crc)
{
    Modbus_AppendCrc(buf, len_without_crc);
    System_UartWrite(buf, (uint8_t)(len_without_crc + 2U));
}

static void Modbus_SendException(uint8_t fn, uint8_t code)
{
    uint8_t tx[5];

    tx[0] = AOM_SLAVE_ID;
    tx[1] = (uint8_t)(fn | 0x80U);
    tx[2] = code;
    Modbus_Send(tx, 3U);
}

static uint8_t Modbus_ReadReg(uint16_t addr, uint16_t *value)
{
    if(addr >= AOM_REG_COUNT)
        return 1U;

    if(addr == AOM_REG_ACTUAL)
    {
        if(s_sig.remote_active)
            s_sig.actual = Signal_Evaluate();
        s_sig.regs[AOM_REG_ACTUAL] = s_sig.actual;
    }

    *value = s_sig.regs[addr];
    return 0U;
}

static uint8_t Modbus_WriteReg(uint16_t addr, uint16_t value)
{
    if(addr >= AOM_REG_COUNT || addr == AOM_REG_ACTUAL)
        return 1U;

    s_sig.regs[addr] = value;
    return 0U;
}

static void Modbus_HandleRead(const uint8_t *frame)
{
    uint16_t start = ((uint16_t)frame[2] << 8U) | frame[3];
    uint16_t qty = ((uint16_t)frame[4] << 8U) | frame[5];
    uint8_t tx[AOM_TX_MAX];
    uint8_t i;

    if(qty == 0U || qty > 24U || (uint16_t)(3U + qty * 2U + 2U) > AOM_TX_MAX)
    {
        Modbus_SendException(frame[1], 3U);
        return;
    }

    tx[0] = AOM_SLAVE_ID;
    tx[1] = frame[1];
    tx[2] = (uint8_t)(qty * 2U);
    for(i = 0U; i < qty; i++)
    {
        uint16_t value;

        if(Modbus_ReadReg((uint16_t)(start + i), &value))
        {
            Modbus_SendException(frame[1], 2U);
            return;
        }
        tx[3U + (i * 2U)] = (uint8_t)(value >> 8U);
        tx[4U + (i * 2U)] = (uint8_t)(value & 0xFFU);
    }

    Modbus_Send(tx, (uint8_t)(3U + qty * 2U));
}

static void Modbus_AfterWrite(uint16_t start, uint16_t qty)
{
    uint16_t end = (uint16_t)(start + qty);

    if(start < AOM_REG_ACTUAL || end > AOM_REG_STEP_START)
        Signal_ConfigChanged();
}

static void Modbus_HandleWriteSingle(const uint8_t *frame, uint8_t broadcast)
{
    uint16_t addr = ((uint16_t)frame[2] << 8U) | frame[3];
    uint16_t value = ((uint16_t)frame[4] << 8U) | frame[5];
    uint8_t tx[8];
    uint8_t i;

    if(Modbus_WriteReg(addr, value))
    {
        if(!broadcast)
            Modbus_SendException(frame[1], 2U);
        return;
    }

    Modbus_AfterWrite(addr, 1U);
    if(broadcast)
        return;

    for(i = 0U; i < 6U; i++)
        tx[i] = frame[i];
    Modbus_Send(tx, 6U);
}

static void Modbus_HandleWriteMulti(const uint8_t *frame, uint8_t broadcast)
{
    uint16_t start = ((uint16_t)frame[2] << 8U) | frame[3];
    uint16_t qty = ((uint16_t)frame[4] << 8U) | frame[5];
    uint8_t byte_count = frame[6];
    uint8_t tx[8];
    uint8_t i;

    if(qty == 0U || qty > AOM_REG_COUNT ||
       start >= AOM_REG_COUNT || (uint16_t)(start + qty) > AOM_REG_COUNT ||
       (start <= AOM_REG_ACTUAL && (uint16_t)(start + qty) > AOM_REG_ACTUAL) ||
       byte_count != (uint8_t)(qty * 2U))
    {
        if(!broadcast)
            Modbus_SendException(frame[1], 3U);
        return;
    }

    for(i = 0U; i < qty; i++)
    {
        uint16_t value = ((uint16_t)frame[7U + i * 2U] << 8U) | frame[8U + i * 2U];

        if(Modbus_WriteReg((uint16_t)(start + i), value))
        {
            if(!broadcast)
                Modbus_SendException(frame[1], 2U);
            return;
        }
    }

    Modbus_AfterWrite(start, qty);
    if(broadcast)
        return;

    tx[0] = AOM_SLAVE_ID;
    tx[1] = frame[1];
    tx[2] = frame[2];
    tx[3] = frame[3];
    tx[4] = frame[4];
    tx[5] = frame[5];
    Modbus_Send(tx, 6U);
}

static void Modbus_ProcessFrame(const uint8_t *frame)
{
    uint8_t broadcast = (frame[0] == 0U) ? 1U : 0U;

    if(frame[0] != AOM_SLAVE_ID && !broadcast)
        return;

    switch(frame[1])
    {
    case MODBUS_FC_READ_HOLDING:
        if(!broadcast)
            Modbus_HandleRead(frame);
        break;
    case MODBUS_FC_WRITE_SINGLE:
        Modbus_HandleWriteSingle(frame, broadcast);
        break;
    case MODBUS_FC_WRITE_MULTI:
        Modbus_HandleWriteMulti(frame, broadcast);
        break;
    default:
        if(!broadcast)
            Modbus_SendException(frame[1], 1U);
        break;
    }
}

static void Modbus_ShiftRx(uint8_t count)
{
    uint8_t i;

    __disable_irq();
    if(count >= s_rx_len)
    {
        s_rx_len = 0U;
        __enable_irq();
        return;
    }

    for(i = 0U; i < (uint8_t)(s_rx_len - count); i++)
        s_rx[i] = s_rx[i + count];
    s_rx_len = (uint8_t)(s_rx_len - count);
    __enable_irq();
}

static uint8_t Modbus_ExpectedLen(void)
{
    if(s_rx_len < 2U)
        return 0U;

    switch(s_rx[1])
    {
    case MODBUS_FC_READ_HOLDING:
    case MODBUS_FC_WRITE_SINGLE:
        return 8U;
    case MODBUS_FC_WRITE_MULTI:
        if(s_rx_len < 7U)
            return 0U;
        if(s_rx[6] > (uint8_t)(AOM_RX_MAX - 9U))
            return 0xFFU;
        return (uint8_t)(9U + s_rx[6]);
    default:
        return 0xFFU;
    }
}

static void Modbus_ClearRx(void)
{
    __disable_irq();
    s_rx_len = 0U;
    __enable_irq();
}

static void Modbus_TryParse(void)
{
    uint8_t expected;

    while(s_rx_len >= 2U)
    {
        if(s_rx[0] != AOM_SLAVE_ID && s_rx[0] != 0U)
        {
            Modbus_ShiftRx(1U);
            continue;
        }

        expected = Modbus_ExpectedLen();
        if(expected == 0U)
            return;
        if(expected == 0xFFU || expected > AOM_RX_MAX)
        {
            Modbus_ShiftRx(1U);
            continue;
        }
        if(s_rx_len < expected)
            return;

        if(Modbus_Crc16(s_rx, (uint8_t)(expected - 2U)) ==
           (uint16_t)(((uint16_t)s_rx[expected - 1U] << 8U) | s_rx[expected - 2U]))
        {
            Modbus_ProcessFrame(s_rx);
            Modbus_ShiftRx(expected);
        }
        else
        {
            Modbus_ShiftRx(1U);
        }
    }
}

static void App_EnterHome(void)
{
    s_app.view = VIEW_HOME;
    App_InvalidateUi();
    App_DrawHomeIfDirty();
}

static void App_EnterMainMenu(void)
{
    s_app.view = VIEW_MAIN_MENU;
    s_app.menu_sel = 0;
    Ui_DrawMainMenu(s_app.menu_sel);
}

static void App_EnterModeMenu(void)
{
    s_app.view = VIEW_MODE_MENU;
    s_app.mode_sel = App_ModeToIndex(Settings_Get()->output_mode);
    Ui_DrawModeMenu(s_app.mode_sel);
}

static void App_EnterCalMode(void)
{
    s_app.view = VIEW_CAL_MODE;
    s_app.cal_sel = App_ModeToIndex(Settings_Get()->output_mode);
    Ui_DrawCalModeMenu(s_app.cal_sel);
}

static void App_EnterSignalMenu(void)
{
    s_app.view = VIEW_SIGNAL_MENU;
    s_app.signal_sel = 0U;
    Signal_NormalizeConfig();
    App_DrawSignalMenu();
}

static void App_EnterSignalEdit(void)
{
    s_app.view = VIEW_SIGNAL_EDIT;
    App_DrawSignalEdit();
}

static void App_EnterCalZero(void)
{
    const SettingsCal_t *cal;

    Signal_DisableRemote();
    s_app.cal_mode = s_modes[s_app.cal_sel];
    cal = Settings_GetCal(s_app.cal_mode);
    s_app.cal_zero = cal->zero_code;
    s_app.cal_full = cal->full_code;
    s_app.cal_err = 0;

    if(!GP8630_IsReady() || GP8630_GetMode() != s_app.cal_mode)
    {
        s_app.view = VIEW_CAL_WAIT;
        GP8630_BeginInitWithMode(s_app.cal_mode);
        Ui_DrawMessage("CAL mode", "Wait...");
        return;
    }

    s_app.view = VIEW_CAL_ZERO;
    GP8630_RequestCode(s_app.cal_zero);
    Ui_DrawCalAdjust(s_app.cal_mode, 0, s_app.cal_zero);
}

static void App_EnterCalZeroReady(void)
{
    s_app.view = VIEW_CAL_ZERO;
    GP8630_RequestCode(s_app.cal_zero);
    Ui_DrawCalAdjust(s_app.cal_mode, 0, s_app.cal_zero);
}

static void App_EnterCalFull(void)
{
    s_app.view = VIEW_CAL_FULL;
    GP8630_RequestCode(s_app.cal_full);
    Ui_DrawCalAdjust(s_app.cal_mode, 1, s_app.cal_full);
}

static void App_EnterCalSave(void)
{
    s_app.view = VIEW_CAL_SAVE;
    s_app.cal_err = 0;
    Ui_DrawCalSave(s_app.cal_mode, s_app.cal_zero, s_app.cal_full, 0);
}

static void App_EnterBrightness(void)
{
    s_app.view = VIEW_BRIGHTNESS;
    s_app.brightness = Settings_Get()->brightness;
    Ui_DrawBrightness(s_app.brightness);
}

static void App_HandleBrightness(int16_t diff, uint8_t click, uint8_t long_click)
{
    int32_t b;

    if(long_click)
    {
        SSD1306_SetContrast(Settings_Get()->brightness);
        App_EnterMainMenu();
        return;
    }

    if(diff != 0)
    {
        b = (int32_t)s_app.brightness + (int32_t)diff * APP_BRIGHTNESS_STEP;
        if(b < (int32_t)SETTINGS_BRIGHTNESS_MIN)
            b = SETTINGS_BRIGHTNESS_MIN;
        if(b > (int32_t)SETTINGS_BRIGHTNESS_MAX)
            b = SETTINGS_BRIGHTNESS_MAX;
        s_app.brightness = (uint8_t)b;
        SSD1306_SetContrast(s_app.brightness);
        Ui_DrawBrightness(s_app.brightness);
        return;
    }

    if(click)
    {
        Settings_SetBrightness(s_app.brightness);
        Settings_RequestSave();
        App_EnterMainMenu();
    }
}

static void App_OnModeSave(void)
{
    GP8630_OutputMode_t mode = s_modes[s_app.mode_sel];

    Signal_DisableRemote();
    Settings_SetMode(mode);
    Settings_Save();
    GP8630_BeginInitWithMode(mode);
    s_app.gp_reconfig = 1;
    s_app.view = VIEW_HOME;
    Ui_DrawMessage("Mode saved", "Reconfig...");
    App_InvalidateUi();
}

static void App_RestoreRunMode(void)
{
    if(GP8630_GetMode() != Settings_Get()->output_mode)
    {
        GP8630_BeginInitWithMode(Settings_Get()->output_mode);
        s_app.gp_reconfig = 1;
        s_app.view = VIEW_HOME;
        Ui_DrawMessage("Exit CAL", "Reconfig...");
        App_InvalidateUi();
        return;
    }

    App_ApplyOutput();
    App_EnterHome();
}

static void App_BootUpdate(void)
{
    const Settings_t *cfg;

    switch(s_app.state)
    {
    case APP_ST_BOOT_DELAY:
        if((System_GetTick() - s_app.state_tick) >= APP_BOOT_DELAY_MS)
        {
            LOGI("app boot -> oled init\r\n");
            s_app.state = APP_ST_OLED_INIT;
        }
        break;

    case APP_ST_OLED_INIT:
        if(SSD1306_Init() == 0)
        {
            SSD1306_SetContrast(Settings_Get()->brightness);
            LOGI("ssd1306 init ok\r\n");
            s_app.state = APP_ST_OLED_DRAW;
        }
        else
        {
            LOGE("ssd1306 init fail\r\n");
            s_app.state = APP_ST_GP8630_START;
        }
        break;

    case APP_ST_OLED_DRAW:
        Ui_DrawBootStep(s_app.draw_step >> 3);
        if(++s_app.draw_step >= APP_BOOT_DRAW_TICKS)
            s_app.state = APP_ST_GP8630_START;
        break;

    case APP_ST_GP8630_START:
        cfg = Settings_Get();
        LOGI("app boot -> gp8630 start mode=%u\r\n", (unsigned)cfg->output_mode);
        GP8630_BeginInitWithMode(cfg->output_mode);
        s_app.state = APP_ST_GP8630_WAIT;
        break;

    case APP_ST_GP8630_WAIT:
        if(GP8630_GetInitState() == GP8630_INIT_OK)
        {
            App_ApplyOutput();
            s_app.state = APP_ST_RUN;
            App_EnterHome();
            LOGI("app boot -> run\r\n");
            Power_Touch();
        }
        else if(GP8630_GetInitState() == GP8630_INIT_FAIL)
        {
            s_app.state = APP_ST_RUN;
            App_EnterHome();
            LOGE("app boot gp8630 fail -> run\r\n");
            Power_Touch();
        }
        break;

    default:
        break;
    }
}

static void App_HandleHome(int16_t diff, uint8_t click, uint8_t long_click)
{
    int32_t permille;
    uint16_t step;
    int16_t min;
    int16_t max;

    if(long_click)
    {
        App_EnterMainMenu();
        return;
    }

    if(click)
    {
        s_app.step_level++;
        if(s_app.step_level >= APP_STEP_LEVEL_COUNT)
            s_app.step_level = 0;
        App_DrawHomeIfDirty();
        return;
    }

    if(diff != 0)
    {
        step = App_GetPermilleStep(Settings_Get()->output_mode);
        permille = Settings_Get()->permille;
        if(diff > 0)
            permille += step;
        else
            permille -= step;

        Signal_DisableRemote();
        min = Settings_GetPermilleMin(Settings_Get()->output_mode);
        max = Settings_GetPermilleMax(Settings_Get()->output_mode);
        if(permille < min)
            permille = min;
        if(permille > max)
            permille = max;

        Settings_SetPermille((int16_t)permille);
        Settings_RequestSave();
        App_ApplyOutput();
        App_DrawHomeIfDirty();
    }
}

static void App_HandleMainMenu(int16_t diff, uint8_t click, uint8_t long_click)
{
    if(long_click)
    {
        App_EnterHome();
        return;
    }

    if(diff != 0)
    {
        s_app.menu_sel = App_AdjustSel(s_app.menu_sel, diff, APP_MAIN_MENU_COUNT);
        Ui_DrawMainMenu(s_app.menu_sel);
        return;
    }

    if(!click)
        return;

    switch(s_app.menu_sel)
    {
    case 0:
        App_EnterModeMenu();
        break;
    case 1:
        App_EnterSignalMenu();
        break;
    case 2:
        App_EnterCalMode();
        break;
    case 3:
        App_EnterBrightness();
        break;
    case 4:
        s_app.view = VIEW_STATUS;
        Ui_DrawStatus(GP8630_GetInitStateStr(), GP8630_GetI2cAddr(), I2C_Bus_GetErrCount());
        break;
    default:
        App_EnterHome();
        break;
    }
}

static void App_HandleModeMenu(int16_t diff, uint8_t click, uint8_t long_click)
{
    if(long_click)
    {
        App_EnterMainMenu();
        return;
    }

    if(diff != 0)
    {
        s_app.mode_sel = App_AdjustSel(s_app.mode_sel, diff, 2);
        Ui_DrawModeMenu(s_app.mode_sel);
        return;
    }

    if(click)
        App_OnModeSave();
}

static void App_OnSignalChanged(uint8_t apply)
{
    Signal_NormalizeConfig();
    if(apply && Signal_IsRemoteActive())
        Signal_ConfigChanged();
}

static void App_HandleSignalMenu(int16_t diff, uint8_t click, uint8_t long_click)
{
    if(long_click)
    {
        App_EnterMainMenu();
        return;
    }

    if(diff != 0)
    {
        s_app.signal_sel = App_AdjustSel(s_app.signal_sel, diff, APP_SIGNAL_MENU_COUNT);
        App_DrawSignalMenu();
        return;
    }

    if(!click)
        return;

    if(s_app.signal_sel == 0U)
    {
        if(Signal_IsRemoteActive())
            Signal_DisableRemote();
        else
        {
            App_EnsureLocalSignalWave();
            Signal_ConfigChanged();
        }
        App_DrawSignalMenu();
        return;
    }

    App_EnterSignalEdit();
}

static void App_HandleSignalEdit(int16_t diff, uint8_t click, uint8_t long_click)
{
    uint8_t mode;

    if(long_click || click)
    {
        s_app.view = VIEW_SIGNAL_MENU;
        App_DrawSignalMenu();
        return;
    }

    if(diff == 0)
        return;

    mode = (uint8_t)s_sig.regs[AOM_REG_SIGNAL_TYPE];
    if(mode > AOM_MODE_VOLTAGE)
        mode = AOM_MODE_CURRENT;

    switch(s_app.signal_sel)
    {
    case 1:
        s_sig.regs[AOM_REG_SIGNAL_TYPE] = (mode == AOM_MODE_CURRENT) ? AOM_MODE_VOLTAGE : AOM_MODE_CURRENT;
        mode = (uint8_t)s_sig.regs[AOM_REG_SIGNAL_TYPE];
        s_sig.regs[AOM_REG_VALUE_A] = Signal_ClampRaw(mode, s_sig.regs[AOM_REG_VALUE_A]);
        s_sig.regs[AOM_REG_VALUE_B] = Signal_ClampRaw(mode, s_sig.regs[AOM_REG_VALUE_B]);
        break;
    case 2:
        s_sig.regs[AOM_REG_WAVEFORM] = App_AdjustLocalWave((uint8_t)s_sig.regs[AOM_REG_WAVEFORM], diff);
        break;
    case 3:
        s_sig.regs[AOM_REG_VALUE_A] = App_AdjustU16(s_sig.regs[AOM_REG_VALUE_A], diff, APP_SIGNAL_VALUE_STEP, Signal_ModeMinRaw(mode), Signal_ModeMaxRaw(mode));
        break;
    case 4:
        s_sig.regs[AOM_REG_VALUE_B] = App_AdjustU16(s_sig.regs[AOM_REG_VALUE_B], diff, APP_SIGNAL_VALUE_STEP, Signal_ModeMinRaw(mode), Signal_ModeMaxRaw(mode));
        break;
    case 5:
        s_sig.regs[AOM_REG_PERIOD_MS] = App_AdjustU16(s_sig.regs[AOM_REG_PERIOD_MS], diff, APP_SIGNAL_PERIOD_STEP, APP_SIGNAL_PERIOD_MIN, APP_SIGNAL_PERIOD_MAX);
        break;
    default:
        break;
    }

    App_OnSignalChanged(1U);
    App_DrawSignalEdit();
}

static void App_HandleCalMode(int16_t diff, uint8_t click, uint8_t long_click)
{
    if(long_click)
    {
        if(GP8630_GetMode() != Settings_Get()->output_mode)
            App_RestoreRunMode();
        else
            App_EnterMainMenu();
        return;
    }

    if(diff != 0)
    {
        s_app.cal_sel = App_AdjustSel(s_app.cal_sel, diff, 2);
        Ui_DrawCalModeMenu(s_app.cal_sel);
        return;
    }

    if(click)
        App_EnterCalZero();
}

static void App_HandleCalZero(int16_t diff, uint8_t click, uint8_t long_click)
{
    if(long_click)
    {
        if(GP8630_GetMode() != Settings_Get()->output_mode)
        {
            App_RestoreRunMode();
            return;
        }
        App_ApplyOutput();
        App_EnterCalMode();
        return;
    }

    if(diff != 0)
    {
        s_app.cal_zero = App_AdjustCode(s_app.cal_zero, diff);
        GP8630_RequestCode(s_app.cal_zero);
        Ui_DrawCalAdjust(s_app.cal_mode, 0, s_app.cal_zero);
        return;
    }

    if(click)
        App_EnterCalFull();
}

static void App_HandleCalFull(int16_t diff, uint8_t click, uint8_t long_click)
{
    if(long_click)
    {
        GP8630_RequestCode(s_app.cal_zero);
        s_app.view = VIEW_CAL_ZERO;
        Ui_DrawCalAdjust(s_app.cal_mode, 0, s_app.cal_zero);
        return;
    }

    if(diff != 0)
    {
        s_app.cal_full = App_AdjustCode(s_app.cal_full, diff);
        GP8630_RequestCode(s_app.cal_full);
        Ui_DrawCalAdjust(s_app.cal_mode, 1, s_app.cal_full);
        return;
    }

    if(click)
        App_EnterCalSave();
}

static void App_HandleCalSave(uint8_t click, uint8_t long_click)
{
    if(long_click)
    {
        App_EnterCalFull();
        return;
    }

    if(!click)
        return;

    if(Settings_SetCal(s_app.cal_mode, s_app.cal_zero, s_app.cal_full))
    {
        Ui_DrawCalSave(s_app.cal_mode, s_app.cal_zero, s_app.cal_full, 1);
        return;
    }

    Settings_Save();
    App_RestoreRunMode();
}

static void App_AfterReconfigOk(void)
{
    if(Signal_IsRemoteActive())
        Signal_ApplyCurrent();
    else
        App_ApplyOutput();

    if(s_app.view == VIEW_SIGNAL_MENU)
    {
        App_DrawSignalMenu();
        return;
    }

    if(s_app.view == VIEW_SIGNAL_EDIT)
    {
        App_DrawSignalEdit();
        return;
    }

    App_EnterHome();
}

static void App_RunUpdate(void)
{
    int16_t diff;
    uint8_t click;
    uint8_t long_click;

    if(s_app.gp_reconfig)
    {
        if(GP8630_GetInitState() == GP8630_INIT_OK)
        {
            s_app.gp_reconfig = 0;
            App_AfterReconfigOk();
        }
        else if(GP8630_GetInitState() == GP8630_INIT_FAIL)
        {
            s_app.gp_reconfig = 0;
            if(s_app.view == VIEW_SIGNAL_MENU)
                App_DrawSignalMenu();
            else if(s_app.view == VIEW_SIGNAL_EDIT)
                App_DrawSignalEdit();
            else
                App_EnterHome();
            LOGE("gp8630 reconfig fail\r\n");
        }
        return;
    }

    diff = Encoder_GetDiff();
    click = Encoder_GetClick();
    long_click = Encoder_GetLongClick();

    switch(s_app.view)
    {
    case VIEW_HOME:
        App_HandleHome(diff, click, long_click);
        App_DrawHomeIfDirty();
        break;
    case VIEW_MAIN_MENU:
        App_HandleMainMenu(diff, click, long_click);
        break;
    case VIEW_MODE_MENU:
        App_HandleModeMenu(diff, click, long_click);
        break;
    case VIEW_CAL_MODE:
        App_HandleCalMode(diff, click, long_click);
        break;
    case VIEW_CAL_WAIT:
        if(GP8630_GetInitState() == GP8630_INIT_OK)
            App_EnterCalZeroReady();
        else if(GP8630_GetInitState() == GP8630_INIT_FAIL)
            App_RestoreRunMode();
        break;
    case VIEW_CAL_ZERO:
        App_HandleCalZero(diff, click, long_click);
        break;
    case VIEW_CAL_FULL:
        App_HandleCalFull(diff, click, long_click);
        break;
    case VIEW_CAL_SAVE:
        App_HandleCalSave(click, long_click);
        break;
    case VIEW_BRIGHTNESS:
        App_HandleBrightness(diff, click, long_click);
        break;
    case VIEW_SIGNAL_MENU:
        App_HandleSignalMenu(diff, click, long_click);
        break;
    case VIEW_SIGNAL_EDIT:
        App_HandleSignalEdit(diff, click, long_click);
        break;
    case VIEW_STATUS:
        if(click || long_click)
            App_EnterMainMenu();
        break;
    default:
        App_EnterHome();
        break;
    }
}

void App_Begin(void)
{
    s_app.state = APP_ST_BOOT_DELAY;
    s_app.view = VIEW_HOME;
    s_app.state_tick = System_GetTick();
    s_app.draw_step = 0;
    s_app.gp_reconfig = 0;
    s_app.step_level = APP_STEP_LEVEL_NORM;
    App_InvalidateUi();

    Settings_Init();
    Signal_Init();
    LOGI("app begin permille=%d mode=%u\r\n",
         (int)Settings_Get()->permille,
         (unsigned)Settings_Get()->output_mode);
}

void App_Update(void)
{
    if(s_app.state != APP_ST_RUN)
    {
        App_BootUpdate();
    }
    else
    {
        App_RunUpdate();
        Signal_Update();
    }
}

void App_OnSerialByte(uint8_t byte)
{
    uint32_t now = System_GetTick();

    if(s_rx_len != 0U && (now - s_rx_tick) > AOM_RX_TIMEOUT_MS)
        s_rx_len = 0U;
    s_rx_tick = now;

    if(s_rx_len >= AOM_RX_MAX)
        s_rx_len = 0U;
    s_rx[s_rx_len++] = byte;
}

void App_SerialPoll(void)
{
    if(s_rx_len == 0U)
        return;

    if((System_GetTick() - s_rx_tick) > AOM_RX_TIMEOUT_MS)
    {
        Modbus_ClearRx();
        return;
    }

    Modbus_TryParse();
}

AppState_t App_GetState(void)
{
    return s_app.state;
}

const char *App_GetStateStr(void)
{
    switch(s_app.state)
    {
    case APP_ST_BOOT_DELAY:
        return "BOOT_DELAY";
    case APP_ST_OLED_INIT:
        return "OLED_INIT";
    case APP_ST_OLED_DRAW:
        return "OLED_DRAW";
    case APP_ST_GP8630_START:
        return "GP_START";
    case APP_ST_GP8630_WAIT:
        return "GP_WAIT";
    case APP_ST_RUN:
        return "RUN";
    default:
        return "?";
    }
}
