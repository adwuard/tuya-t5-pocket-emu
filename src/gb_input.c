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

// Forward declaration
extern int ev_postevent(event_t *ev);

// Hardware pin definitions (from tuya_t5ai_pocket.c)
#define JOYSTICK_ADC_NUM    TUYA_ADC_NUM_0
#define JOYSTICK_ADC_CH_X   15
#define JOYSTICK_ADC_CH_Y   14
#define JOYSTICK_BUTTON_PIN TUYA_GPIO_NUM_9

// Pin definitions swapped: Physical A button is on GPIO 19, SELECT is on GPIO 17
#define BUTTON_A_PIN      TUYA_GPIO_NUM_19 // Swapped: Physical A is on GPIO 19
#define BUTTON_B_PIN      TUYA_GPIO_NUM_18
#define BUTTON_SELECT_PIN TUYA_GPIO_NUM_17 // Swapped: Physical SELECT is on GPIO 17
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
#define ADC_WIDTH           12   // 12-bit ADC (but actual hardware range is wider)
#define ADC_MIN_VALUE       0    // Minimum expected ADC value
#define ADC_MAX_VALUE       8192 // Maximum expected ADC value (wider than 12-bit for safety)
#define JOYSTICK_THRESHOLD  300  // Threshold for direction detection (±300 from center)
#define CALIBRATION_SAMPLES 20   // Number of samples to average for center calibration
#define ADC_NOISE_THRESHOLD 50   // Ignore small variations below this threshold

// Simple absolute value macro
#define ABS(x) ((x) < 0 ? -(x) : (x))

static bool    input_initialized = false;
static bool    adc_calibrated    = false;
static uint8_t current_buttons   = 0;
static uint8_t previous_buttons  = 0;
static INT32_T adc_center_x      = 0; // Dynamically calibrated center X
static INT32_T adc_center_y      = 0; // Dynamically calibrated center Y
static INT32_T adc_min_x         = 0; // Minimum observed X value
static INT32_T adc_max_x         = 0; // Maximum observed X value
static INT32_T adc_min_y         = 0; // Minimum observed Y value
static INT32_T adc_max_y         = 0; // Maximum observed Y value
static INT32_T last_raw_adc_x    = 0;
static INT32_T last_raw_adc_y    = 0;
static int16_t last_joy_x        = 0; // Last processed joystick X offset
static int16_t last_joy_y        = 0; // Last processed joystick Y offset
static bool    last_btn_a        = false;
static bool    last_btn_b        = false;
static bool    last_btn_sel      = false;
static bool    last_btn_start    = false;

// GNUBoy input states
extern char keystates[MAX_KEYS];

/**
 * @brief Calibrate ADC center position by averaging initial samples and finding range
 */
static void calibrate_adc_center(void)
{
    INT32_T     sum_x = 0, sum_y = 0;
    INT32_T     adc_buf_x[1] = {0};
    INT32_T     adc_buf_y[1] = {0};
    OPERATE_RET ret;
    int         valid_samples_x = 0, valid_samples_y = 0;
    INT32_T     min_x = 999999, max_x = 0;
    INT32_T     min_y = 999999, max_y = 0;

    // Take multiple samples and average them, also track min/max
    // Note: Channels are swapped - X (LEFT/RIGHT) uses channel 14, Y (UP/DOWN) uses channel 15
    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
        // X axis (LEFT/RIGHT) - use channel 14 (was Y)
        ret = tkl_adc_read_single_channel(JOYSTICK_ADC_NUM, JOYSTICK_ADC_CH_Y, adc_buf_x);
        if (ret == 0 && adc_buf_x[0] >= ADC_MIN_VALUE && adc_buf_x[0] <= ADC_MAX_VALUE) {
            sum_x += adc_buf_x[0];
            valid_samples_x++;
            if (adc_buf_x[0] < min_x)
                min_x = adc_buf_x[0];
            if (adc_buf_x[0] > max_x)
                max_x = adc_buf_x[0];
        }

        // Y axis (UP/DOWN) - use channel 15 (was X)
        ret = tkl_adc_read_single_channel(JOYSTICK_ADC_NUM, JOYSTICK_ADC_CH_X, adc_buf_y);
        if (ret == 0 && adc_buf_y[0] >= ADC_MIN_VALUE && adc_buf_y[0] <= ADC_MAX_VALUE) {
            sum_y += adc_buf_y[0];
            valid_samples_y++;
            if (adc_buf_y[0] < min_y)
                min_y = adc_buf_y[0];
            if (adc_buf_y[0] > max_y)
                max_y = adc_buf_y[0];
        }

        // Small delay between samples
        tal_system_sleep(10);
    }

    if (valid_samples_x > 0 && valid_samples_y > 0) {
        adc_center_x   = sum_x / valid_samples_x;
        adc_center_y   = sum_y / valid_samples_y;
        adc_min_x      = min_x;
        adc_max_x      = max_x;
        adc_min_y      = min_y;
        adc_max_y      = max_y;
        adc_calibrated = true;
    } else {
        // Fallback to observed midpoint if calibration fails
        adc_center_x   = 4200; // Typical midpoint from user observation
        adc_center_y   = 4200;
        adc_min_x      = 1700;
        adc_max_x      = 6900;
        adc_min_y      = 1700;
        adc_max_y      = 6900;
        adc_calibrated = true;
    }
}

/**
 * @brief Read joystick position from ADC using generic ADC read function
 */
static void read_joystick_adc(int16_t *x, int16_t *y, INT32_T *raw_x, INT32_T *raw_y)
{
    INT32_T     adc_x = 0, adc_y = 0;
    OPERATE_RET ret;
    INT32_T     adc_buf_x[1]  = {0};
    INT32_T     adc_buf_y[1]  = {0};
    static int  error_count_x = 0, error_count_y = 0;

    // Read X channel (LEFT/RIGHT) - swapped: use channel 14 (was Y)
    // Note: tkl_adc_read_single_channel returns 0 on success (OPRT_OK)
    ret = tkl_adc_read_single_channel(JOYSTICK_ADC_NUM, JOYSTICK_ADC_CH_Y, adc_buf_x);
    if (ret == 0) {
        // Accept wider range (1700-6900 observed range)
        if (adc_buf_x[0] >= ADC_MIN_VALUE && adc_buf_x[0] <= ADC_MAX_VALUE) {
            adc_x         = adc_buf_x[0];
            error_count_x = 0; // Reset error count on success
        } else {
            // Out of expected range, clamp to calibrated center
            adc_x = adc_center_x;
            if (error_count_x++ < 3) {
            }
        }
    } else {
        // Read failed, use calibrated center
        adc_x = adc_center_x;
        if (error_count_x++ < 3) {
        }
    }

    // Read Y channel (UP/DOWN) - swapped: use channel 15 (was X)
    ret = tkl_adc_read_single_channel(JOYSTICK_ADC_NUM, JOYSTICK_ADC_CH_X, adc_buf_y);
    if (ret == 0) {
        // Accept wider range (1700-6900 observed range)
        if (adc_buf_y[0] >= ADC_MIN_VALUE && adc_buf_y[0] <= ADC_MAX_VALUE) {
            adc_y         = adc_buf_y[0];
            error_count_y = 0; // Reset error count on success
        } else {
            // Out of expected range, clamp to calibrated center
            adc_y = adc_center_y;
            if (error_count_y++ < 3) {
            }
        }
    } else {
        // Read failed, use calibrated center
        adc_y = adc_center_y;
        if (error_count_y++ < 3) {
        }
    }

    // Store raw values for debugging (use actual read buffer values, not processed values)
    if (raw_x)
        *raw_x = adc_buf_x[0]; // Store actual ADC read value for debugging
    if (raw_y)
        *raw_y = adc_buf_y[0]; // Store actual ADC read value for debugging

    // Convert ADC values to signed offsets from dynamically calibrated center
    // X axis (LEFT/RIGHT) - channel 14: Large number = RIGHT, Small number = LEFT
    *x = (int16_t)(adc_x - adc_center_x);
    // Y axis (UP/DOWN) - channel 15: Large number = UP, Small number = DOWN (inverted)
    *y = (int16_t)(adc_center_y - adc_y);
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
 * @brief Map hardware inputs to Game Boy buttons (one-liner key read)
 */
static uint8_t map_input_to_gb(void)
{
    uint8_t buttons = 0;
    int16_t joy_x = 0, joy_y = 0;
    INT32_T raw_adc_x = 0, raw_adc_y = 0;
    bool    btn_a = 0, btn_b = 0, btn_sel = 0, btn_start = 0;

    // Read joystick position from ADC (with raw values for debugging)
    read_joystick_adc(&joy_x, &joy_y, &raw_adc_x, &raw_adc_y);

    // Read all GPIO buttons in one pass
    btn_a     = read_gpio_button(BUTTON_A_PIN);
    btn_b     = read_gpio_button(BUTTON_B_PIN);
    btn_sel   = read_gpio_button(BUTTON_SELECT_PIN);
    btn_start = read_gpio_button(BUTTON_START_PIN);

    // Map joystick to D-pad with ±300 threshold from calibrated center
    // Apply noise filtering: only trigger if change is significant
    int16_t joy_x_filtered = joy_x;
    int16_t joy_y_filtered = joy_y;

    // Filter out small variations (noise) - only use if change is significant
    if (ABS(joy_x - last_joy_x) < ADC_NOISE_THRESHOLD) {
        joy_x_filtered = last_joy_x; // Keep previous value if change is too small
    } else {
        joy_x_filtered = joy_x; // Use new value if change is significant
    }
    if (ABS(joy_y - last_joy_y) < ADC_NOISE_THRESHOLD) {
        joy_y_filtered = last_joy_y; // Keep previous value if change is too small
    } else {
        joy_y_filtered = joy_y; // Use new value if change is significant
    }

    // Update last values
    last_joy_x = joy_x_filtered;
    last_joy_y = joy_y_filtered;

    // Map filtered joystick values to D-pad
    if (joy_x_filtered < -JOYSTICK_THRESHOLD) {
        buttons |= BUTTON_LEFT;
    } else if (joy_x_filtered > JOYSTICK_THRESHOLD) {
        buttons |= BUTTON_RIGHT;
    }

    if (joy_y_filtered < -JOYSTICK_THRESHOLD) {
        buttons |= BUTTON_UP;
    } else if (joy_y_filtered > JOYSTICK_THRESHOLD) {
        buttons |= BUTTON_DOWN;
    }

    // Map GPIO buttons
    if (btn_a)
        buttons |= BUTTON_A;
    if (btn_b)
        buttons |= BUTTON_B;
    if (btn_sel)
        buttons |= BUTTON_SELECT;
    if (btn_start)
        buttons |= BUTTON_START;

    // Update last values for change detection
    last_raw_adc_x = raw_adc_x;
    last_raw_adc_y = raw_adc_y;
    last_btn_a     = btn_a;
    last_btn_b     = btn_b;
    last_btn_sel   = btn_sel;
    last_btn_start = btn_start;

    return buttons;
}

/**
 * @brief Update GNUBoy input states by posting events to event queue
 * This matches SDL2 behavior: ev_poll() posts events, emulator processes them via ev_getevent()
 * Note: This should only be called when browser is not active
 */
static void update_gnuboy_input(uint8_t buttons)
{
    event_t ev;
    uint8_t changed = buttons ^ previous_buttons;

    // Post events for changed buttons (like SDL2 does)
    // Browser reads keystates[] directly, so we still update it for browser use
    // D-pad directions
    if (changed & BUTTON_UP) {
        ev.type = (buttons & BUTTON_UP) ? EV_PRESS : EV_RELEASE;
        ev.code = K_JOYUP;
        ev_postevent(&ev);
    }
    if (changed & BUTTON_DOWN) {
        ev.type = (buttons & BUTTON_DOWN) ? EV_PRESS : EV_RELEASE;
        ev.code = K_JOYDOWN;
        ev_postevent(&ev);
    }
    if (changed & BUTTON_LEFT) {
        ev.type = (buttons & BUTTON_LEFT) ? EV_PRESS : EV_RELEASE;
        ev.code = K_JOYLEFT;
        ev_postevent(&ev);
    }
    if (changed & BUTTON_RIGHT) {
        ev.type = (buttons & BUTTON_RIGHT) ? EV_PRESS : EV_RELEASE;
        ev.code = K_JOYRIGHT;
        ev_postevent(&ev);
    }

    // Swapped: A button maps to K_JOY2 (SELECT), SELECT maps to K_JOY0 (A)
    // K_JOY0 = Game Boy A, K_JOY1 = Game Boy B, K_JOY2 = Game Boy SELECT, K_JOY3 = Game Boy START
    if (changed & BUTTON_A) {
        ev.type = (buttons & BUTTON_A) ? EV_PRESS : EV_RELEASE;
        ev.code = K_JOY2; // Physical A → Game Boy SELECT
        ev_postevent(&ev);
    }
    if (changed & BUTTON_B) {
        ev.type = (buttons & BUTTON_B) ? EV_PRESS : EV_RELEASE;
        ev.code = K_JOY1; // Physical B → Game Boy B
        ev_postevent(&ev);
    }
    if (changed & BUTTON_START) {
        ev.type = (buttons & BUTTON_START) ? EV_PRESS : EV_RELEASE;
        ev.code = K_JOY3; // Physical START → Game Boy START
        ev_postevent(&ev);
    }
    if (changed & BUTTON_SELECT) {
        ev.type = (buttons & BUTTON_SELECT) ? EV_PRESS : EV_RELEASE;
        ev.code = K_JOY0; // Physical SELECT → Game Boy A
        ev_postevent(&ev);
    }

    previous_buttons = buttons;
}

/**
 * @brief Initialize input
 */
OPERATE_RET gb_input_init(void)
{
    OPERATE_RET          ret = OPRT_OK;
    TUYA_GPIO_BASE_CFG_T gpio_cfg;

    if (input_initialized) {
        return OPRT_OK;
    }

    // Initialize ADC for joystick channels
    // Match board initialization pattern exactly (from tuya_t5ai_pocket.c)
    // The board uses CONTINUOUS mode, so we'll use the same
    TUYA_ADC_BASE_CFG_T adc_cfg;
    memset(&adc_cfg, 0, sizeof(TUYA_ADC_BASE_CFG_T));
    adc_cfg.width = ADC_WIDTH;                 // 12-bit
    adc_cfg.mode  = TUYA_ADC_CONTINUOUS;       // Match board: TUYA_ADC_CONTINUOUS
    adc_cfg.type  = TUYA_ADC_INNER_SAMPLE_VOL; // Match board
    // Configure channel list: channels 14 (Y) and 15 (X) - matching board pattern
    adc_cfg.ch_list.data = (1 << JOYSTICK_ADC_CH_X) | (1 << JOYSTICK_ADC_CH_Y);
    adc_cfg.ch_nums      = 2; // Two channels (matching BOARD_JOYSTICK_ADC_CH_NUM)
    adc_cfg.conv_cnt     = 1; // One conversion per channel (matching board)

    // Note: Board may have already initialized ADC via tdd_joystick_register
    // We initialize here to ensure our channels are registered in the config array
    ret = tkl_adc_init(JOYSTICK_ADC_NUM, &adc_cfg);
    if (ret != OPRT_OK) {
        PR_ERR("ADC initialization failed: %d", ret);
        return ret;
    }

    // Calibrate ADC center position dynamically after initialization
    calibrate_adc_center();

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

    // Only post events to emulator if browser is not active
    // Browser takes control of input when active
    extern bool gb_browser_is_active(void);
    if (!gb_browser_is_active()) {
        // Update GNUBoy input system (posts events to emulator)
        update_gnuboy_input(current_buttons);
    } else {
        // Browser is active - still update keystates[] for browser use
        // but don't post events to emulator
        extern char keystates[MAX_KEYS];
        keystates[K_JOYUP]    = (current_buttons & BUTTON_UP) ? 1 : 0;
        keystates[K_JOYDOWN]  = (current_buttons & BUTTON_DOWN) ? 1 : 0;
        keystates[K_JOYLEFT]  = (current_buttons & BUTTON_LEFT) ? 1 : 0;
        keystates[K_JOYRIGHT] = (current_buttons & BUTTON_RIGHT) ? 1 : 0;
        keystates[K_JOY0]     = (current_buttons & BUTTON_SELECT) ? 1 : 0;
        keystates[K_JOY1]     = (current_buttons & BUTTON_B) ? 1 : 0;
        keystates[K_JOY2]     = (current_buttons & BUTTON_A) ? 1 : 0;
        keystates[K_JOY3]     = (current_buttons & BUTTON_START) ? 1 : 0;
    }

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
