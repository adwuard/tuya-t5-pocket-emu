/**
 * @file gb_browser.c
 * @brief SD Card ROM Browser with Input Navigation
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_fs.h"
#include "tkl_pinmux.h"
#include "lv_vendor.h"
#include "gb_browser.h"
#include "gb_input.h"
#include "gb_display.h"
#include "gb_emu.h"
#include "input.h"
#include "lvgl.h"

#include <string.h>
#include <stdlib.h>

#define SDCARD_MOUNT_PATH "/sdcard"
#define ROM_DIR_PATH      "/sdcard/roms"
#define MAX_FILES         100
#define MAX_PATH_LEN      256
#define MAX_FILENAME_LEN  64

// Display dimensions (T5 Pocket) - same as gb_display.c
#define DISP_WIDTH  384
#define DISP_HEIGHT 168

// ROM file extensions
#define ROM_EXT_GB  ".gb"
#define ROM_EXT_GBC ".gbc"

// Browser state
typedef struct {
    bool      initialized;
    bool      active;
    bool      sd_mounted;
    lv_obj_t *list;
    lv_obj_t *label_title;
    char     *file_paths[MAX_FILES];
    char     *file_names[MAX_FILES];
    int       file_count;
    int       selected_index;
    int       scroll_offset;
    bool      input_handled;
    uint32_t  last_input_time;
} browser_state_t;

static browser_state_t browser = {0};

// Input handling
#define INPUT_REPEAT_DELAY_MS 200
#define INPUT_DEBOUNCE_MS     50

static uint32_t last_up_time     = 0;
static uint32_t last_down_time   = 0;
static uint32_t last_select_time = 0;
static uint32_t last_back_time   = 0;

/**
 * @brief Case-insensitive string comparison
 */
static int strcasecmp_tuya(const char *s1, const char *s2)
{
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') {
            c1 += 32; // Convert to lowercase
        }
        if (c2 >= 'A' && c2 <= 'Z') {
            c2 += 32; // Convert to lowercase
        }
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

/**
 * @brief Check if file is a ROM file
 */
static bool is_rom_file(const char *filename)
{
    if (filename == NULL) {
        return false;
    }

    size_t len = strlen(filename);
    if (len < 3) {
        return false;
    }

    const char *ext = filename + len - 3;
    return (strcasecmp_tuya(ext, ROM_EXT_GB) == 0 || strcasecmp_tuya(ext, ROM_EXT_GBC) == 0);
}

/**
 * @brief Scan directory for ROM files
 */
static int scan_rom_files(const char *dir_path)
{
    TUYA_DIR      dir;
    TUYA_FILEINFO info = NULL; // Initialize to NULL (TUYA_FILEINFO is void*)
    OPERATE_RET   ret;

    // Clear previous file list
    for (int i = 0; i < browser.file_count; i++) {
        if (browser.file_paths[i]) {
            tal_free(browser.file_paths[i]);
            browser.file_paths[i] = NULL;
        }
        if (browser.file_names[i]) {
            tal_free(browser.file_names[i]);
            browser.file_names[i] = NULL;
        }
    }
    browser.file_count = 0;

    // Open directory
    ret = tkl_dir_open(dir_path, &dir);
    if (ret != 0) { // TKL FS APIs return 0 on success
        PR_ERR("Failed to open directory: %s, ret=%d", dir_path, ret);
        return 0;
    }

    int         count      = 0;
    const char *name       = NULL;
    BOOL_T      is_regular = FALSE;

    // Read directory entries
    while (tkl_dir_read(dir, &info) == 0 && count < MAX_FILES) { // 0 = success
        // Check if info is valid before using it
        if (info == NULL) {
            PR_WARN("tkl_dir_read returned NULL info");
            break; // End of directory or error
        }

        // Get file name
        ret = tkl_dir_name(info, &name);
        if (ret != 0 || name == NULL) { // 0 = success
            continue;
        }

        // Check if it's a regular file
        ret = tkl_dir_is_regular(info, &is_regular);
        if (ret != 0 || !is_regular) { // 0 = success
            continue;
        }

        // Check if it's a ROM file
        if (is_rom_file(name)) {
            // Allocate and store file path
            char full_path[MAX_PATH_LEN];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, name);

            browser.file_paths[count] = (char *)tal_malloc(strlen(full_path) + 1);
            if (browser.file_paths[count] == NULL) {
                PR_ERR("Failed to allocate memory for file path");
                continue; // Skip this file if allocation fails
            }
            strcpy(browser.file_paths[count], full_path);

            // Allocate and store file name
            browser.file_names[count] = (char *)tal_malloc(strlen(name) + 1);
            if (browser.file_names[count] == NULL) {
                PR_ERR("Failed to allocate memory for file name");
                tal_free(browser.file_paths[count]); // Free path if name allocation fails
                browser.file_paths[count] = NULL;
                continue; // Skip this file if allocation fails
            }
            strcpy(browser.file_names[count], name);

            count++;
        }
    }

    tkl_dir_close(dir);
    browser.file_count = count;

    PR_NOTICE("Found %d ROM files in %s", count, dir_path);
    return count;
}

/**
 * @brief Update list widget with file names
 */
static void update_list_widget(void)
{
    if (browser.list == NULL) {
        return;
    }

    // Clear existing items
    lv_obj_clean(browser.list);

    // Calculate how many items can fit on screen
    // With 384px height: title (25px) + instructions (20px) + margins (15px) = ~324px for list
    // Each list item is roughly 20-25px, so we can show about 12-15 items
    int items_per_screen = 12;
    int start_idx        = browser.scroll_offset;
    int end_idx          = start_idx + items_per_screen;
    if (end_idx > browser.file_count) {
        end_idx = browser.file_count;
    }

    // Add file names to list
    for (int i = start_idx; i < end_idx; i++) {
        if (browser.file_names[i]) {
            // LVGL v9 uses lv_list_add_button
            lv_obj_t *item = lv_list_add_button(browser.list, NULL, browser.file_names[i]);
            if (item == NULL) {
                PR_ERR("Failed to add list item %d", i);
                continue;
            }
            // Style the list item for better spacing
            lv_obj_set_style_pad_all(item, 3, 0);

            if (i == browser.selected_index) {
                lv_obj_add_state(item, LV_STATE_FOCUSED);
                // Make selected item more visible with background
                lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_FOCUSED);

                // Add border to highlight selected item
                lv_obj_set_style_border_width(item, 2, LV_STATE_FOCUSED);
                lv_obj_set_style_border_color(item, lv_color_white(), LV_STATE_FOCUSED);

                // Add underline effect using a thicker bottom border on the button
                // This is simpler and more reliable than accessing child labels
                lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, LV_STATE_FOCUSED);
                lv_obj_set_style_border_width(item, 3, LV_STATE_FOCUSED); // Thicker bottom border for underline
            }
        }
    }

    // Update scroll position to keep selected item visible
    if (browser.selected_index >= browser.scroll_offset + items_per_screen) {
        browser.scroll_offset = browser.selected_index - items_per_screen + 1;
        if (browser.scroll_offset < 0) {
            browser.scroll_offset = 0;
        }
    } else if (browser.selected_index < browser.scroll_offset) {
        browser.scroll_offset = browser.selected_index;
    }
}

/**
 * @brief Create browser UI (improved style following ebook_screen.c)
 */
static void create_browser_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    if (scr == NULL) {
        PR_ERR("Failed to get screen object");
        return;
    }
    lv_obj_set_size(scr, DISP_WIDTH, DISP_HEIGHT);

    lv_vendor_disp_lock();

    // Hide GB canvas if it exists (we'll show it again when emulator runs)
    // gb_canvas is defined in gb_display.c as non-static
    extern lv_obj_t *gb_canvas; // From gb_display.c
    if (gb_canvas != NULL) {
        lv_obj_add_flag(gb_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    // Clear screen for browser UI
    lv_obj_clean(scr);

// Layout constants (following ebook_screen.c style)
#define TITLE_HEIGHT    20
#define INSTR_HEIGHT    20
#define LIST_MARGIN     10
#define LIST_TOP_OFFSET (TITLE_HEIGHT + 5)
#define LIST_HEIGHT     (DISP_HEIGHT - LIST_TOP_OFFSET - INSTR_HEIGHT - 10)

    // Create title label at top
    browser.label_title = lv_label_create(scr);
    if (browser.label_title == NULL) {
        PR_ERR("Failed to create title label");
        lv_vendor_disp_unlock();
        return;
    }
    lv_label_set_text(browser.label_title, "Select ROM");
    lv_obj_align(browser.label_title, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_text_align(browser.label_title, LV_TEXT_ALIGN_CENTER, 0);

    // Create list widget with proper sizing for 384px display
    if (browser.file_count > 0) {
        browser.list = lv_list_create(scr);
        if (browser.list == NULL) {
            PR_ERR("Failed to create list widget");
            lv_vendor_disp_unlock();
            return;
        }
        // Size list to fill available space between title and instructions
        lv_obj_set_size(browser.list, DISP_WIDTH - 10, LIST_HEIGHT);
        lv_obj_align(browser.list, LV_ALIGN_TOP_MID, 0, LIST_TOP_OFFSET);
        lv_obj_set_style_pad_all(browser.list, 5, 0);
        lv_obj_set_style_pad_row(browser.list, 2, 0); // Row spacing between items

        // Update list with files
        update_list_widget();

        // Create instructions label at bottom
        lv_obj_t *instr_label = lv_label_create(scr);
        if (instr_label) {
            lv_label_set_text(instr_label, "UP/DOWN: Navigate  SELECT: Choose");
            lv_obj_align(instr_label, LV_ALIGN_BOTTOM_MID, 0, -5);
            lv_obj_set_style_text_align(instr_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(instr_label, lv_color_make(150, 150, 150), 0);
        }
    } else {
        // Show message if no ROMs found with better styling
        lv_obj_t *msg_label = lv_label_create(scr);
        if (msg_label) {
            lv_label_set_text(msg_label, "No ROMs found!\n\nPlace .gb or .gbc files\nin /sdcard/roms/");
            lv_obj_align(msg_label, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_text_align(msg_label, LV_TEXT_ALIGN_CENTER, 0);
        }
        browser.list = NULL;
    }

    // Note: LVGL runs in its own thread, so we don't need to call lv_task_handler() here
    // The LVGL thread will automatically process the UI updates
    lv_vendor_disp_unlock();
    PR_NOTICE("Browser UI created successfully");
}

/**
 * @brief Handle browser input
 */
void gb_browser_handle_input(void)
{
    if (!browser.active) {
        return;
    }

    uint32_t    now = tal_system_get_millisecond();
    extern char keystates[MAX_KEYS];

    // Handle UP (scroll up)
    if (keystates[K_JOYUP] && (now - last_up_time) > INPUT_REPEAT_DELAY_MS) {
        last_up_time = now;
        if (browser.selected_index > 0) {
            browser.selected_index--;
            update_list_widget();
        }
    } else if (!keystates[K_JOYUP]) {
        last_up_time = 0;
    }

    // Handle DOWN (scroll down)
    if (keystates[K_JOYDOWN] && (now - last_down_time) > INPUT_REPEAT_DELAY_MS) {
        last_down_time = now;
        if (browser.selected_index < browser.file_count - 1) {
            browser.selected_index++;
            update_list_widget();
        }
    } else if (!keystates[K_JOYDOWN]) {
        last_down_time = 0;
    }

    // Handle SELECT (choose ROM)
    if (keystates[K_JOY2] && (now - last_select_time) > INPUT_DEBOUNCE_MS) {
        last_select_time = now;
        // Selection will be handled by gb_browser_show()
    } else if (!keystates[K_JOY2]) {
        last_select_time = 0;
    }

    // Handle START/B (back/cancel)
    if (keystates[K_JOY3] && (now - last_back_time) > INPUT_DEBOUNCE_MS) {
        last_back_time = now;
        browser.active = false;
    } else if (!keystates[K_JOY3]) {
        last_back_time = 0;
    }
}

/**
 * @brief Initialize SD card and browser
 */
OPERATE_RET gb_browser_init(void)
{
    OPERATE_RET ret = OPRT_OK;

    if (browser.initialized) {
        return OPRT_OK;
    }

    PR_NOTICE("Initializing SD card browser...");

    // Configure SDIO pins (from tuya_t5ai_pocket.c)
    // Note: These pins are also used for buttons, but SDIO needs them configured first
    // The board_register_hardware() should have already configured them, but we ensure it here
    tkl_io_pinmux_config(TUYA_GPIO_NUM_14, TUYA_SDIO_HOST_CLK);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_15, TUYA_SDIO_HOST_CMD);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_16, TUYA_SDIO_HOST_D0);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_17, TUYA_SDIO_HOST_D1);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_18, TUYA_SDIO_HOST_D2);
    tkl_io_pinmux_config(TUYA_GPIO_NUM_19, TUYA_SDIO_HOST_D3);

    PR_NOTICE("SDIO pins configured");

    // Mount SD card
    ret = tkl_fs_mount(SDCARD_MOUNT_PATH, DEV_SDCARD);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to mount SD card: %d", ret);
        return ret;
    }

    browser.sd_mounted = true;
    PR_NOTICE("SD card mounted successfully");

    // Create ROM directory if it doesn't exist
    // Check if directory exists first
    BOOL_T dir_exists = FALSE;
    if (tkl_fs_is_exist(ROM_DIR_PATH, &dir_exists) == 0 && !dir_exists) {
        // Directory doesn't exist, create it
        if (tkl_fs_mkdir(ROM_DIR_PATH) != 0) {
            PR_WARN("Failed to create ROM directory: %s", ROM_DIR_PATH);
            // Continue anyway - directory might already exist or be created later
        }
    }

    browser.initialized = true;
    PR_NOTICE("SD card browser initialized");

    return OPRT_OK;
}

/**
 * @brief Deinitialize browser
 */
void gb_browser_deinit(void)
{
    if (!browser.initialized) {
        return;
    }

    // Unmount SD card
    if (browser.sd_mounted) {
        tkl_fs_unmount(SDCARD_MOUNT_PATH);
        browser.sd_mounted = false;
    }

    // Free file list
    for (int i = 0; i < browser.file_count; i++) {
        if (browser.file_paths[i]) {
            tal_free(browser.file_paths[i]);
            browser.file_paths[i] = NULL;
        }
        if (browser.file_names[i]) {
            tal_free(browser.file_names[i]);
            browser.file_names[i] = NULL;
        }
    }

    browser.initialized = false;
    browser.active      = false;
    PR_NOTICE("SD card browser deinitialized");
}

/**
 * @brief Show ROM browser UI
 */
void gb_browser_show(void)
{
    if (!browser.initialized) {
        PR_ERR("Browser not initialized");
        return;
    }

    // Wait a bit more to ensure SD card is fully ready
    tal_system_sleep(50);

    // Scan for ROM files
    PR_NOTICE("Scanning for ROM files...");
    int file_count = scan_rom_files(ROM_DIR_PATH);
    PR_NOTICE("Scan complete: found %d ROM files", file_count);

    if (file_count == 0) {
        PR_WARN("No ROM files found in %s", ROM_DIR_PATH);
        // Don't return - show empty browser UI so user knows the system is working
        // The UI will show a message
    }

    // Initialize browser state
    browser.active         = true;
    browser.selected_index = 0;
    browser.scroll_offset  = 0;

    // Wait a bit before creating UI to ensure everything is stable
    PR_NOTICE("Waiting before UI creation...");
    tal_system_sleep(200);

    // Wait a bit to ensure display is ready
    // Note: LVGL runs in its own thread, so we don't need to call lv_task_handler() here
    tal_system_sleep(50);

    // Create UI
    PR_NOTICE("Starting UI creation...");
    create_browser_ui();
    PR_NOTICE("UI creation complete");
}

/**
 * @brief Get selected ROM path (if user selected one)
 */
char *gb_browser_get_selected(void)
{
    if (!browser.active) {
        return NULL;
    }

    extern char     keystates[MAX_KEYS];
    uint32_t        now              = tal_system_get_millisecond();
    static uint32_t last_select_time = 0;

    // Check for selection (SELECT button)
    if (keystates[K_JOY2] && (now - last_select_time) > INPUT_DEBOUNCE_MS) {
        last_select_time = now;

        // User selected a ROM
        if (browser.selected_index >= 0 && browser.selected_index < browser.file_count) {
            char *selected_path = (char *)tal_malloc(strlen(browser.file_paths[browser.selected_index]) + 1);
            if (selected_path) {
                strcpy(selected_path, browser.file_paths[browser.selected_index]);
            }
            browser.active = false;
            return selected_path;
        }
    } else if (!keystates[K_JOY2]) {
        last_select_time = 0;
    }

    return NULL;
}

/**
 * @brief Update browser (call in main loop when browser is active)
 */
void gb_browser_update(void)
{
    if (!browser.active) {
        return;
    }

    // Note: LVGL runs in its own thread, so we don't need to call lv_task_handler() here
    // The LVGL thread will automatically process UI updates
    // We only need to handle input and update browser state
}

/**
 * @brief Check if browser is active
 */
bool gb_browser_is_active(void)
{
    return browser.active;
}
