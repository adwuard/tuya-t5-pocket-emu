/**
 * @file gb_input.c
 * @brief Game Boy Input Adapter using Direct ADC and GPIO Access
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_adc.h"
#include "tkl_gpio.h"
#include "gb_input.h"

#include "sys.h"
#include "input.h"
#include "defs.h"
#include "io.h"
#include <string.h>

// Hardware pin definitions (from tuya_t5ai_pocket.c)
#define JOYSTICK_ADC_NUM    TUYA_ADC_NUM_0
#define JOYSTICK_ADC_CH_X   15
#define JOYSTICK_ADC_CH_Y   14
#define JOYSTICK_BUTTON_PIN TUYA_GPIO_NUM_9

#define BUTTON_A_PIN      TUYA_GPIO_NUM_17
#define BUTTON_B_PIN      TUYA_GPIO_NUM_18
#define BUTTON_SELECT_PIN TUYA_GPIO_NUM_19
#define BUTTON_START_PIN  TUYA_GPIO_NUM_26

// Button active levels (all active low with pull-up)
#define BUTTON_ACTIVE_LEVEL   TUYA_GPIO_LEVEL_LOW
#define BUTTON_INACTIVE_LEVEL TUYA_GPIO_LEVEL_HIGH

// Button mapping
#define BUTTON_UP     0x01
#define BUTTON_DOWN   0x02
#define BUTTON_LEFT   0x04
#define BUTTON_RIGHT  0x08
#define BUTTON_A      0x10
#define BUTTON_B      0x20
#define BUTTON_SELECT 0x40
#define BUTTON_START  0x80

// Joystick configuration
#define ADC_WIDTH          12                     // 12-bit ADC
#define ADC_MAX_VALUE      ((1 << ADC_WIDTH) - 1) // 4095
#define ADC_CENTER         (ADC_MAX_VALUE / 2)    // 2048
#define JOYSTICK_DEADZONE  200                    // Dead zone around center
#define JOYSTICK_THRESHOLD 800                    // Threshold for direction detection

static bool    input_initialized = false;
static uint8_t current_buttons   = 0;
static uint8_t previous_buttons  = 0;

// GNUBoy input states
extern char keystates[MAX_KEYS];

/**
 * @brief Read joystick position from ADC
 */
static void read_joystick_adc(int16_t *x, int16_t *y)
{
    INT32_T     adc_x = 0, adc_y = 0;
    OPERATE_RET ret;

    // Read X channel (channel 15)
    ret = tkl_adc_read_single_channel(JOYSTICK_ADC_NUM, JOYSTICK_ADC_CH_X, &adc_x);
    if (ret != OPRT_OK) {
        adc_x = ADC_CENTER; // Default to center on error
    }

    // Read Y channel (channel 14)
    ret = tkl_adc_read_single_channel(JOYSTICK_ADC_NUM, JOYSTICK_ADC_CH_Y, &adc_y);
    if (ret != OPRT_OK) {
        adc_y = ADC_CENTER; // Default to center on error
    }

    // Convert ADC values to signed offsets from center
    // ADC values are typically 0-4095, center is ~2048
    *x = (int16_t)(adc_x - ADC_CENTER);
    *y = (int16_t)(adc_y - ADC_CENTER);
}

/**
 * @brief Read GPIO button state
 */
static bool read_gpio_button(TUYA_GPIO_NUM_E pin)
{
    TUYA_GPIO_LEVEL_E level;
    OPERATE_RET       ret;

    ret = tkl_gpio_read(pin, &level);
    if (ret != OPRT_OK) {
        return false; // Default to not pressed on error
    }

    // Button is active low (pressed = LOW, released = HIGH)
    return (level == BUTTON_ACTIVE_LEVEL);
}

/**
 * @brief Map hardware inputs to Game Boy buttons
 */
static uint8_t map_input_to_gb(void)
{
    uint8_t buttons = 0;
    int16_t joy_x = 0, joy_y = 0;

    // Read joystick position from ADC
    read_joystick_adc(&joy_x, &joy_y);

    // Map joystick to D-pad with dead zone
    if (joy_x < -JOYSTICK_THRESHOLD) {
        buttons |= BUTTON_LEFT;
    } else if (joy_x > JOYSTICK_THRESHOLD) {
        buttons |= BUTTON_RIGHT;
    }

    if (joy_y < -JOYSTICK_THRESHOLD) {
        buttons |= BUTTON_UP;
    } else if (joy_y > JOYSTICK_THRESHOLD) {
        buttons |= BUTTON_DOWN;
    }

    // Read GPIO buttons
    if (read_gpio_button(BUTTON_A_PIN)) {
        buttons |= BUTTON_A;
    }
    if (read_gpio_button(BUTTON_B_PIN)) {
        buttons |= BUTTON_B;
    }
    if (read_gpio_button(BUTTON_SELECT_PIN)) {
        buttons |= BUTTON_SELECT;
    }
    if (read_gpio_button(BUTTON_START_PIN)) {
        buttons |= BUTTON_START;
    }

    return buttons;
}

/**
 * @brief Update GNUBoy input states
 */
static void update_gnuboy_input(uint8_t buttons)
{
    // Map to GNUBoy input system
    // GNUBoy uses io.c for button input via keystates array
    // Update keystates based on button changes

    uint8_t changed = buttons ^ previous_buttons;

    if (changed & BUTTON_UP) {
        keystates[K_JOYUP] = (buttons & BUTTON_UP) ? 1 : 0;
    }
    if (changed & BUTTON_DOWN) {
        keystates[K_JOYDOWN] = (buttons & BUTTON_DOWN) ? 1 : 0;
    }
    if (changed & BUTTON_LEFT) {
        keystates[K_JOYLEFT] = (buttons & BUTTON_LEFT) ? 1 : 0;
    }
    if (changed & BUTTON_RIGHT) {
        keystates[K_JOYRIGHT] = (buttons & BUTTON_RIGHT) ? 1 : 0;
    }
    if (changed & BUTTON_A) {
        keystates[K_JOY1] = (buttons & BUTTON_A) ? 1 : 0;
    }
    if (changed & BUTTON_B) {
        keystates[K_JOY0] = (buttons & BUTTON_B) ? 1 : 0;
    }
    if (changed & BUTTON_START) {
        keystates[K_JOY3] = (buttons & BUTTON_START) ? 1 : 0;
    }
    if (changed & BUTTON_SELECT) {
        keystates[K_JOY2] = (buttons & BUTTON_SELECT) ? 1 : 0;
    }

    previous_buttons = buttons;
}

/**
 * @brief Initialize input
 */
OPERATE_RET gb_input_init(void)
{
    OPERATE_RET          ret = OPRT_OK;
    TUYA_ADC_BASE_CFG_T  adc_cfg;
    TUYA_GPIO_BASE_CFG_T gpio_cfg;

    if (input_initialized) {
        return OPRT_OK;
    }

    PR_NOTICE("Initializing GB input...");

    // Initialize ADC for joystick
    memset(&adc_cfg, 0, sizeof(TUYA_ADC_BASE_CFG_T));
    adc_cfg.width = ADC_WIDTH;
    adc_cfg.mode  = TUYA_ADC_SINGLE; // Single-shot mode for polling
    adc_cfg.type  = TUYA_ADC_INNER_SAMPLE_VOL;

    ret = tkl_adc_init(JOYSTICK_ADC_NUM, &adc_cfg);
    if (ret != OPRT_OK) {
        PR_ERR("ADC initialization failed: %d", ret);
        return ret;
    }

    // Initialize GPIO for buttons (input mode with pull-up)
    memset(&gpio_cfg, 0, sizeof(TUYA_GPIO_BASE_CFG_T));
    gpio_cfg.mode   = TUYA_GPIO_PULLUP;     // Input with pull-up
    gpio_cfg.direct = TUYA_GPIO_INPUT;      // Input direction
    gpio_cfg.level  = TUYA_GPIO_LEVEL_HIGH; // Default high (not pressed)

    // Initialize button GPIOs
    ret = tkl_gpio_init(BUTTON_A_PIN, &gpio_cfg);
    if (ret != OPRT_OK) {
        PR_ERR("Button A GPIO init failed: %d", ret);
    }

    ret = tkl_gpio_init(BUTTON_B_PIN, &gpio_cfg);
    if (ret != OPRT_OK) {
        PR_ERR("Button B GPIO init failed: %d", ret);
    }

    ret = tkl_gpio_init(BUTTON_SELECT_PIN, &gpio_cfg);
    if (ret != OPRT_OK) {
        PR_ERR("Button SELECT GPIO init failed: %d", ret);
    }

    ret = tkl_gpio_init(BUTTON_START_PIN, &gpio_cfg);
    if (ret != OPRT_OK) {
        PR_ERR("Button START GPIO init failed: %d", ret);
    }

    // Initialize joystick button GPIO (if needed)
    ret = tkl_gpio_init(JOYSTICK_BUTTON_PIN, &gpio_cfg);
    if (ret != OPRT_OK) {
        PR_ERR("Joystick button GPIO init failed: %d", ret);
    }

    // Initialize GNUBoy input system
    kb_init();
    joy_init();

    // Clear button states
    current_buttons  = 0;
    previous_buttons = 0;
    memset(keystates, 0, sizeof(keystates));

    input_initialized = true;
    PR_NOTICE("GB input initialized (ADC + GPIO)");

    return OPRT_OK;
}

/**
 * @brief Deinitialize input
 */
void gb_input_deinit(void)
{
    if (!input_initialized) {
        return;
    }

    // Deinitialize GPIO
    tkl_gpio_deinit(BUTTON_A_PIN);
    tkl_gpio_deinit(BUTTON_B_PIN);
    tkl_gpio_deinit(BUTTON_SELECT_PIN);
    tkl_gpio_deinit(BUTTON_START_PIN);
    tkl_gpio_deinit(JOYSTICK_BUTTON_PIN);

    // Deinitialize ADC
    tkl_adc_deinit(JOYSTICK_ADC_NUM);

    joy_close();
    kb_close();

    input_initialized = false;
    PR_NOTICE("GB input deinitialized");
}

/**
 * @brief Poll input (called from main loop)
 */
void gb_input_poll(void)
{
    if (!input_initialized) {
        return;
    }

    // Read current button states
    current_buttons = map_input_to_gb();

    // Update GNUBoy input system
    update_gnuboy_input(current_buttons);

    // Poll GNUBoy input system
    kb_poll();
    joy_poll();
}

// GNUBoy sys.h interface implementations

void ev_poll(void)
{
    gb_input_poll();
}

void joy_init(void)
{
    // Joystick is initialized via Tuya hardware registration
}

void joy_poll(void)
{
    // Input polling is handled in gb_input_poll()
}

void joy_close(void)
{
    // Cleanup if needed
}

void kb_init(void)
{
    // Keyboard initialization (not applicable for embedded)
}

void kb_poll(void)
{
    // Keyboard polling (not applicable for embedded)
}

void kb_close(void)
{
    // Keyboard cleanup (not applicable for embedded)
}
