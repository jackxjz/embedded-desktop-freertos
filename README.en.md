# ESP32-Based Simple FreeRTOS Operating System

English | [中文](README.md)

This project is a FreeRTOS desktop simulator on an ESP32-S3 development board. It integrates a graphical user interface (LVGL), file system, network synchronization, power management, and other core functions, aiming to demonstrate the design capability of embedded multitasking systems and interactive applications.

This project is a work for the **2026 Electronic Science and Technology Association Embedded Group Assessment**.

---

## Assessment Requirements Cross-Reference Table

### About the Biggest Design Change

Assessment 3.1 requires "use one input device to complete cursor movement, confirmation, return, scrolling, or equivalent operations", and 3.2.2 requires "connect a real keyboard or equivalent input device".
Due to limited development time, this work **does not include a self-made mouse, nor does it include a real keyboard driver**. Instead:

- It directly uses the onboard **GT911 capacitive touchscreen** of the development board as the only input device, letting LVGL draw the touch point as a cursor — touch simultaneously handles "cursor movement + left-click confirmation";
- It uses **long press (1000 ms)** as the equivalent of right-click, and **touch dragging** as the equivalent of mouse dragging;
- It uses the **LVGL soft keyboard** for all text input.

This trade-off is allowed in the assessment document ("input device form is customizable... or equivalent solutions"). I will also explain it item by item in the following requirements.

### Basic Requirements

| Assessment Requirement | Status | Notes |
| --- | --- | --- |
| Developed with FreeRTOS | ✅ | 3 task types: `lvgl`, `wifi_sync`, `save_draw`. LVGL is protected by a **recursive mutex** `lvgl_mux` |
| Power-on shows boot screen → password screen → desktop | ✅ | Boot screen, version number, and progress bar; fades into the login page after 1.5 seconds; enters the desktop after successful login |
| Password error prompt + lockout after consecutive errors | ✅ | Password errors 3 times will lock for 10 seconds. During lockout, input boxes, login/register/account management buttons are disabled, and the keyboard is hidden; the popup is rebuilt every second to form a countdown |
| Permission control for the account management entry | ✅ | Entering Account Management requires entering the administrator password first; wrong/empty input gives a prompt and buzzer feedback |
| Desktop displays cursor, app icons, status information | ✅ | Includes cursor, file icons + "Settings" + "Drawing" icons, and date/time in the upper-right corner |
| Create/delete files on desktop; duplicate names not allowed | ✅ | Long-press empty desktop → "New"; long-press selected file → "Delete"/"Rename"; duplicate-name validation exists |
| Cursor moves without leaving screen bounds | ⚠️ | **Touch itself is limited by the screen's physical bounds**; the cursor follows the touch point via LVGL, so it naturally cannot go out of bounds. Icon drag positions are clamped to bounds |
| Apps can be selected, opened, and exited back to desktop | ✅ | "Settings" and "Drawing" icons, together with file icons, use the unified flow "select → tap again to open". Each app has an "Exit" in the upper-right corner to return to the desktop |
| Show disconnected state when input device is not connected | N/A | Not implemented; there is no additional input device |
| Input supports move/confirm/release/drag or equivalent | ✅ | Supports move, confirm, release, and drag (icon dragging) |
| System settings: cursor sensitivity / cursor size / brightness / volume | ⚠️ | Implemented: cursor size (8–48 px), volume (0–100), screen-off timeout (10/20/30 s). **Not implemented: cursor sensitivity and screen brightness** (the cursor follows the touch point via LVGL, so cursor sensitivity is not considered; the backlight has no PWM dimming channel) |
| Parameters persist after modification and reboot | ✅ | Cursor size, volume, and screen-off timeout all persist after reboot |
| Manual screen-off + timeout screen-off + input wake-up | ✅ | Desktop has a manual "Screen Off" button; the timeout screen-off time is configurable |
| After screen-off wake-up, returns to a reasonable state without reboot | ✅ | Screen-off only switches the backlight; UI objects are not destroyed, so after wake-up it stays on the original screen |

### Advanced Requirements

| Assessment Requirement | Status | Notes |
| --- | --- | --- |
| Simple drawing app, can save and recover after power loss | ✅ | 480×320 Canvas (307 KB buffer, PSRAM preferred), 6-color palette, press to draw a point/drag to connect lines/release to stop; binary format; restores on power-on |
| Real keyboard or equivalent device for text input; files are actually maintained | 🔁 | Virtual keyboard inputs file names and file contents. **No real keyboard connected, no HID driver written** (the biggest design change for this item) |
| Music playback app, volume linked to system volume | ❌ | No audio codec/amplifier in the project; `lv_demo_music` is only kept as a comment in `main.c` |
| System update app (version number + fake/real OTA; difference observable after upgrade) | ❌ | No version-number screen, no OTA. Only a reusable foundation exists: the drawing file header already contains a `version=1` field |
| Log viewer app (login failures/file add-delete/settings changes/input disconnect-recovery, can be cleared) | ❌ | Only serial `ESP_LOGx` output; no on-screen log app and no persistent log file |
| System monitor app (uptime/status/input event count/error count/task status) | ❌ | `CONFIG_LV_USE_PERF_MONITOR=y` is enabled (LVGL's built-in FPS/CPU overlay), but there is no standalone monitor app |

*✅ means implemented, ⚠️ means partially implemented, 🔁 means equivalently implemented, ❌ means not implemented*

---

## Hardware List

### Main Control Development Board

![ESP32-S3-Touch-LCD-7 Onboard Resources](https://docs.waveshare.net/assets/images/ESP32-S3-Touch-LCD-7-Intro1-1354f76103c5429920a42f7a5ca1dc7d.webp)

| Item     | Specification                                                             |
| -------- | ---------------------------------------------------------------- |
| Model     | ESP32-S3-Touch-LCD-7                                             |
| Manufacturer     | Waveshare                                                 |
| Processor   | High-performance Xtensa 32-bit LX7 dual-core processor, up to 240 MHz             |
| Wireless connectivity | Supports 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5 (LE), onboard antenna |
| Flash    | 8 MB                                                             |
| PSRAM    | 8 MB                                                             |
| Display   | 7-inch capacitive touchscreen                                                 |
| Peripheral interfaces | CAN, RS485, I²C, USB, etc.                                          |
| Development framework | ESP-IDF                                                          |

[Schematic](https://files.waveshare.net/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7-Sch.pdf) | [ESP32-S3-Touch-LCD-7 Drawings](https://www.waveshare.net/wiki/%E6%96%87%E4%BB%B6:ESP32-S3-Touch-LCD-7.zip)

### Other Peripherals

- Buzzer (Fusheng Technology FUET-5020): uses electromagnetic sound generation, rated voltage 3V (operating voltage range 2V~4V), driving frequency 4kHz. SMD package (5×5×2.2mm), suitable for compact embedded designs. Maximum sound pressure level up to 75dB, operating current 110mA, stable operation in the -20℃ to +70℃ temperature range. Used for system event prompts, key feedback, alarm notifications, and other scenarios.

### Main Features

- **Graphical UI**: Boot screen, login/registration, desktop, account management, system settings, drawing app, and soft keyboard built on LVGL.
- **File system**: SPIFFS supports file creation, deletion, editing, and uniqueness validation.
- **Account management**: Local account registration/login, password error lockout mechanism, remember password (stored in NVS); the account management entry requires administrator password verification.
- **Network sync**: Wi-Fi auto-connect, NTP time sync, reconnect after disconnection.
- **Power management**: Timeout auto screen-off + manual screen-off; screen-off time configurable (10/20/30 seconds).
- **System settings**: Cursor size adjustment, NVS-persisted parameters, takes effect immediately.
- **Multitasking**: FreeRTOS dual-core task partitioning (UI response, network background, power monitoring, etc.).

---

## Development Log

All software changes are recorded here.

### v4.4

#### New

- Added boot screen (Screen 0): on power-up, it first displays the `JackOS` logo, system version number, and a boot progress bar. After the progress bar fills, it fades into the login page, fulfilling assessment 3.2.1 "power-on shows boot screen"
- Version number is uniformly provided by `APP_VERSION_STR` in the header (currently `4.4`), globally visible, ready for direct reuse by the future "System Update" app; changing the version only requires editing one place
- All text on the boot screen uses ASCII and the Montserrat font, avoiding missing glyphs and garbled text caused by using an on-demand generated Chinese font subset
- Added administrator password verification to the account management entry: tapping "Account Management" on the login page now requires entering the correct administrator password before entering. If the password is wrong or empty, a prompt and buzzer feedback are given; canceling stays on the login page

#### Improvements

- Fixed the issue where the Wi-Fi disconnect callback blocked the system event loop: the disconnect callback previously directly delayed for 5 seconds before reconnecting, and since that callback runs on the system event task, it froze the entire event loop; now the callback only posts a notification, and a background task handles the delayed reconnection
- Adjusted the creation timing of the Wi-Fi event group: changed from inside the background task to before the task starts, eliminating the potential crash window where "the event precedes the creation of the event group"
- Fixed the issue where long-pressing after sliding on an icon still mistakenly popped up the menu: LVGL's long-press and click events themselves do not check movement; now the maximum movement during the press is recorded manually, so after sliding, long-press no longer pops up the menu and no longer enters drag mode
- Fixed the issue where sliding on a selected icon and releasing would mistakenly open the file: the click determination also includes movement checking, with a looser threshold than long press, to avoid slight finger jitter swallowing normal clicks

### v4.3

#### Improvements

- Fixed screen-off time not persisting after reboot: reading the configuration at boot occurred before SPIFFS was mounted, so it inevitably failed. Configuration reading is now moved to the application layer and executed after the file system is ready
- Adjusted layering: the LVGL adaptation layer no longer directly reads/writes the file system; the screen-off time is injected by the application layer through an interface
- Fixed UI object leaks caused by repeatedly entering and exiting the drawing app: the page object tree was not actually destroyed when exiting drawing, and the desktop page was recreated when returning to the desktop. It now creates on demand and destroys correctly
- Fixed the release order of the drawing canvas buffer: previously the memory was freed before the canvas object was destroyed, leaving a dangling pointer window. It now destroys the object first and then frees the memory
- Fixed a memory leak in the drawing palette color buttons: each color button previously allocated separate memory to store its color value and did not release it on exit; it now uses a static color table
- Fixed repeated creation of the desktop page time-refresh timer and top-layer keyboard: after repeatedly entering and exiting the drawing app, multiple refresh timers and keyboard objects accumulated. The desktop page is no longer reinitialized when it already exists
- Fixed drawing save succeeding only once: the flag controlling the save state was not reset when the background task ended, causing subsequent saves to be silently ignored and exit no longer prompting unsaved changes. It is now correctly reset at task end
- Added a canvas "unsaved changes" flag: the exit confirmation box previously incorrectly relied on the "saving" flag; it now independently records whether the canvas has unsaved content and prompts only when there are changes
- Unified the drawing page "back to desktop" logic into a single exit, eliminating duplicate code

### v4.2

#### Improvements

- Fixed a crash in the account management page that could operate on destroyed objects when refreshing the list: clear the selection state before refreshing, and delete from back to front when clearing the list to avoid index misalignment caused by "deleting while iterating"
- Fixed a memory leak in the account list: the account-name copies stored in list items are now correctly released on refresh and exit
- Fixed a crash when closing the file editor: the message box callback now obtains the message box object from the event itself instead of guessing through parent object hierarchy
- Fixed inconsistency between the "Remember Password" state and actual storage: after successful login, synchronize the remember-password flag so the page state matches stored data
- Fixed cursor size not persisting after reboot: move cursor creation before all screen initialization

### v4.1

#### New

- Drawing save changed to background task execution: on save, quickly copy a canvas snapshot, then hand it to a background task to write to Flash slowly; the main thread returns immediately and the UI no longer freezes
- During saving, a flag blocks repeated clicks to avoid triggering the same save multiple times
- After saving completes, return to the UI thread to show a prompt, and confirm the drawing page still exists (to prevent the user having exited to the desktop during saving)
- When writing files, voluntarily yield the CPU every few dozen lines to avoid the background Flash write starving the UI task

#### Improvements

- Drawing app storage format upgraded from text to binary; file header adds magic (`DRAW`) and size validation, improving data reliability and parsing speed
- Use an 8 KB buffer for batch writes to Flash, reducing write count and extending Flash lifespan
- When "Remember Password" is unchecked, automatically clear saved account credentials in NVS to avoid privacy residue
- Adjusted Wi-Fi configuration structure and optimized connection parameter management
- When opening a drawing, first validate the magic number and canvas size; if they do not match, ignore it as an old file (compatible with remnants of the early text format)

### v4.0

#### New

- Brand-new drawing app; can be entered from a desktop icon into a standalone drawing screen
- 480×320 canvas implemented with LVGL Canvas, centered on the screen
- 6-color brush palette (black, red, green, blue, yellow, orange); the current color is highlighted with a white border
- Supports touch-slide continuous drawing: press to draw a point, drag to connect lines, release to stop; smooth drawing feel
- Drawing content can be saved to SPIFFS; non-white pixels are compressed for storage to save space
- Canvas clear function: one-tap reset to a white canvas. Clearing only changes the canvas content and does not delete the stored file.
- On app startup, automatically loads the last saved drawing content to achieve power-loss recovery
- Complete page layout: title, exit, palette, plus "Save" at lower left and "Clear" at lower right

#### Improvements

- Canvas buffer preferentially uses PSRAM (8 MB), reducing internal RAM usage and improving stability for large canvas handling
- Friendly prompt on memory allocation failure to avoid system crash
- Automatically release canvas buffer when exiting the drawing app to prevent memory leaks

### v3.3

#### New

- Added passive buzzer hardware adaptation to provide system sound feedback
- Buzzer driven by PWM, fixed frequency, volume adjusted by changing duty cycle; maximum output limited to half to avoid harshness
- Settings app adds a volume slider to control system prompt sound level
- Volume defaults to 50, range 0–100, 0 is mute; volume is saved and restored on next boot
- While dragging the volume slider, the buzzer sounds continuously so users can perceive the current volume in real time, giving intuitive feedback
- Added buzzer prompts for scenarios such as password errors, account deletion, and unsaved files to enhance interaction feedback
- File manager adds file rename support for modifying the names of existing files

#### Improvements

- Optimized prompt wording in the project, uniformly correcting "息屏" to "熄屏" to improve accuracy
- Removed duplicate NVS initialization calls in the Wi-Fi time sync module; unified management in the main program entry to avoid redundant initialization

### v3.2

#### New

- Added a manual screen-off button on the desktop; users can manually trigger immediate screen-off
- The desktop "Screen Off" button is placed to the left of the "Exit" button with a 10-pixel gap
- Settings app adds timeout screen-off configuration: 10 s / 20 s / 30 s, three options

#### Improvements

- Refactored power management module to support coexistence of timeout auto screen-off and manual screen-off
- Fixed message box font application logic: changed from global style to separately setting fonts for title, content, and buttons to ensure correct Chinese character rendering

### v3.1

#### New

- Integrated Wi-Fi connection; SSID/password are written directly in a configuration header, connecting in STA mode; connection and time sync run in a background task with a 6 KB stack and priority 5
- Network time is synchronized by polling two servers (pool.ntp.org and ntp.aliyun.com); time zone set to UTC+8
- The time in the upper-right corner of the desktop is updated by a once-per-second timer; date uses a smaller font, time uses a larger font

#### Improvements

- After network time sync completes, automatically refresh the desktop status bar time display
- Optimized settings UI interaction; parameter changes are immediately written to NVS and take effect in real time
- Automatically attempt reconnection after network disconnection; after successful reconnection, automatically resync time

### v3.0

#### New

- Brand-new system settings app; can be entered from a desktop icon into a standalone settings screen
- Implemented cursor size adjustment with multi-level real-time preview and immediate effect
- Settings parameters persisted to NVS to ensure the latest user configuration is retained after reboot

#### Improvements

- Refactored system parameter management architecture; unified NVS read/write interfaces for all modules
- Optimized desktop app launch flow; supports app-level screen navigation and return stack management

### v2.2

#### New

- Implemented timeout screen-off: automatically turns off the screen after 10 seconds of user inactivity
- Screen-off is implemented as a backlight switch, controlling the backlight enable bit through an I²C expander chip
- Supports touch wake-up; after wake-up, restores to a reasonable desktop state

### v2.1

#### New

- Cursor is directly bound to the touch input device; LVGL automatically follows the touch point, with no custom movement logic
- Cursor style is a 16×16 circle with black semi-transparent fill, white border, and black outer stroke, ensuring visibility on both light and dark backgrounds
- Long press acts as the equivalent of "right mouse button"; long-press time set to 1 second
- Desktop icons can be dragged: long-press an unselected icon to enter drag mode; drag range is limited to the desktop container; coordinates are saved on release
- Icon coordinates are stored in SPIFFS and restored by file name on boot
- Long-press empty desktop to show "New" menu; long-press a selected file to show "Delete" menu
- File editor adds unsaved changes prompt: mark as "dirty" on any content change; when closing with changes, show a "Don't Save / Cancel" confirmation box

#### Improvements

- Implemented file uniqueness validation to prevent creating files with duplicate names

### v2.0

#### New

- Brand-new desktop main screen with app icon rendering
- File management based on SPIFFS: create and delete files
- Desktop supports file selection and opening; file editor screen supports exit back to desktop
- File editor supports basic text input and soft keyboard invocation

### v1.3

#### New

- Added password error prompt: after 3 cumulative errors, lock for 10 seconds
- During lockout, show a countdown prompt to enhance user feedback
- During lockout, also disable register and account management buttons and hide the keyboard to prevent further operations
- After unlock, whether to restore the login button depends on whether "Agree to Terms" is checked
- The lockout popup itself has no buttons; it refreshes the countdown by rebuilding once per second
- When the login screen is destroyed, uniformly clean up lockout-related timers and popup references to avoid dangling timers

### v1.2

#### New

- Added user account management page
- Implemented account list page supporting viewing and deleting registered local accounts
- Supports account-level operation: delete account
- Shows "No accounts" when the list is empty

#### Improvements

- Optimized username/password error prompts: ensure a popup appears for wrong username/password or empty password
- Enhanced login page input focus management and soft keyboard adaptation

### v1.1

#### New

- Added user terms confirmation option to login flow
- Login button remains disabled when "Agree to Terms" is not checked
- Account file format is "account,password" one per line; account and password each limited to 50 characters
- After successful registration, clear the three input boxes and automatically return to the login page
- Login form adds "Remember Password"; credentials are encrypted and stored in NVS
- Integrated SPIFFS lightweight file system to support persistent account data storage

#### Improvements

- Optimized registration flow: added username uniqueness validation; on conflict, prompt in real time and prevent duplicate registration

### v1.0

#### New

- Project built on ESP-IDF 5.2.0 and SquareLine Studio 1.5.3 (LVGL 8.3.11); UI code generated by SquareLine, with handwritten logic concentrated in screen initialization functions, widget event callbacks, and extended screen construction functions
- Board support layer split into three parts: screen and touch drivers, LVGL adaptation layer (buffers, mutex, tasks, input device registration), and headers carrying resolution and task parameters
- Display uses anti-tearing mode 3 (LCD double buffering + LVGL direct mode); frame buffers placed in PSRAM
- UI task pinned to CPU1 (priority 2, 6 KB stack); main program remains on CPU0, forming dual-core division of labor
- System tick 1000 Hz, UI heartbeat 2 ms
- Screen supports 0/90/180/270-degree rotation; on rotation, touch coordinate swapping and mirroring are adjusted synchronously
- Compile-time enforcement checks that color depth is 16-bit and no byte swapping is done, consistent with SquareLine project settings

### Supplementary Items That Cannot Be Precisely Attributed to a Version

The following changes cannot be precisely versioned:

| Supplementary Item | Evidence |
| --- | --- |
| Icon position persistence (remember each icon's placement) | Commit `1defa59` |
| Icon dragging | Commit `1defa59` |
| Right-click menu on long press | Commits `1defa59`, `d6eb971` |
| Movement threshold for desktop slide long press (fix "slide long press also shows New menu") | Commit `1defa59` |
| Reverse-order deletion fix for crash after creating a file | Commit `3ef5386` |
| Desktop screen-off button, upper-right date and time display | Commit `3ef5386` |
| File rename | Commit `d6eb971` |
| Clear remembered credentials when deleting account | Commit `f03c3f0` |
| Chinese font subset glyph supplementation (three times total, garbled text recurred) | Commits `2d35adc`, `677d114`, `1240add` |
| Project cleanup: remove build directory, complete `.gitignore` | Commits `7b99800`, `e102527` |
| README restructured and English version added | Commit `4c1e636` |
| Boot order adjustment: buzzer initialization placed after SPIFFS mount, so volume can be correctly restored | Code verification |

---

## Troubleshooting

This section is explained in detail by AI.

### 1: System Immediately Reboots After Creating a File

**Symptom**: Long-press empty desktop → "New" → enter file name and confirm, then **the system immediately reboots**; after reboot it enters the desktop and the file has actually been created successfully (because the crash occurs on the refresh path); creating again still reboots.

**Investigation process**:

Before the crash, the serial log normally prints `SPIFFS: 文件创建成功: /spiffs/usr_xxx.txt`, immediately followed by:

```
Guru Meditation Error: Core 1 panic'ed (LoadProhibited)
PC : 0x4200e26b
EXCVADDR: 0x00000010
--- 0x4200e26b: lv_obj_get_user_data at .../core/lv_obj.h:322
--- (inlined by) refresh_file_icons at .../main/lvgl-file/screens/ui_Screen3.c:329
```

`EXCVADDR = 0x10` is a wild address (dereferencing NULL + 0x10 offset), indicating the obtained object pointer is invalid.

**Cause analysis**: `refresh_file_icons()` first gets a fixed count `cnt` from `lv_obj_get_child_cnt()`, then iterates **forward** with `for (i = 0; i < cnt; i++)`; `lv_obj_del(child)` inside the loop continuously shortens the container's child count, so the index passed to `lv_obj_get_child()` gradually becomes misaligned, eventually obtaining a released/invalid object, then calling `lv_obj_get_user_data()` on it → dereferencing a wild pointer → `LoadProhibited`. The crash occurs on the unique path "refresh desktop after successful file creation", fully matching the phenomenon "always crashes after creation, file still exists after reboot".

**Solution**:

1. Change to **reverse** iteration `for (int32_t i = cnt - 1; i >= 0; i--)`, using signed `int32_t` to avoid `i--` underflowing to `UINT32_MAX` and going out of bounds again;
2. During iteration, skip the two "virtual app icons" (`__settings__`, `__draw__`), otherwise the Settings/Drawing icons would also be deleted;
3. Keep the deletion order as "first `free_icon_user_data(child)` to release `user_data` dynamic memory, then `lv_obj_del(child)`";
4. After the loop, uniformly reset `selected_file_icon`, `dragging_icon`, `drag_in_progress`, `long_press_handled`, `desktop_press_moved`, then call `load_icon_positions()` and rebuild icons according to the new list.

**Current status**: Fixed.

### 2: Cursor Size Does Not Persist After Reboot (Slider Position Correct but Cursor Restores Default)

**Symptom**: Dragging "Cursor Size" in Settings makes the cursor grow/shrink in real time, and the Slider stays at the dragged position; but **after rebooting and entering Settings, the Slider is still at the previous position, while the cursor restores to the default 16×16**.

**Cause analysis**: In `ui_init()`, `ui_Screen5_screen_init()` is executed first, and `ui_cursor` is created afterward. `ui_Screen5_screen_init()` internally calls `apply_cursor_size(saved_size)` to "immediately apply the saved cursor size". At this time `ui_cursor` is still `NULL`, and the function's opening `if (ui_cursor == NULL) return;` makes this call **silently fail**; the cursor is then created at the default size 16. The reason the Slider looks normal is that its value is written directly with `lv_slider_set_value()` during UI initialization, independent of `ui_cursor`.

**Solution**: Move the creation, style setup, `lv_indev_set_cursor()` binding, and initial position of `ui_cursor` **entirely before all `ui_Screen*_screen_init()` calls**, and add `extern lv_obj_t * ui_cursor;` in `ui.h`.

**Current status**: Fixed.

### 3: Screen-Off Time Does Not Persist After Reboot (Always Returns to 10 Seconds)

**Symptom**: Change screen-off time to 20 or 30 seconds in Settings and save; it takes effect at the time; **after reboot the printed log is always "screen-off time: 10 seconds"**, and you must enter Settings and tap once to restore the saved value.

**Cause analysis**: This is an **initialization order** problem. The call order in `main.c` is:

```c
waveshare_esp32_s3_rgb_lcd_init();   // internally → lvgl_port_init() → screen_timeout_load()
init_spiffs();                       // SPIFFS is not mounted until here
...
buzzer_init();                       // volume is read here (order is correct, so volume can be restored)
```

`screen_timeout_load()` called inside `lvgl_port_init()` performs `fopen("/spiffs/screen_timeout.txt")`, but at this point SPIFFS **has not been mounted yet**, so `fopen` necessarily fails, and the function returns the default value `SCREEN_TIMEOUT_DEFAULT` (=10). In the whole project, only the Settings page (`ui_Screen5.c`) calls `lvgl_port_set_screen_timeout()`; the desktop and other screens do not reapply the saved value, so the screen-off time falls back to 10 seconds on every boot.

**Solution**: In `main.c`, after `init_spiffs()` succeeds and before the boot screen, read the saved screen-off time and inject it into the lower layer; at the same time delete the original three lines of reading code in `lvgl_port_init()`, and remove the reference to `wifi_sync.h` in `lvgl_port.c`.

**Current status**: Fixed.

### 4: Message Box Close Button "×" Not Displayed / Chinese Garbled Text (Font Subset Recurrence)

**Symptom**: When Chinese in the message box displays normally, the close button "×" in the upper-right corner is blank; after changing the font to `LV_PART_ITEMS`, "×" appears, but Chinese becomes garbled. The commit message `fix(font): embed missing glyph data to prevent garbled text` **appeared three times** in the repository (`2d35adc`, `677d114`, `1240add`), indicating this is a recurring problem.

**Cause analysis**: `lv_msgbox` consists of two parts—the title/body labels belong to `LV_PART_MAIN`, and the **button matrix (including the close button) belongs to `LV_PART_ITEMS`**. The Chinese font `ui_font_Font1` used by the project is a **subset font generated on demand**; its character set is explicitly listed by the `--symbols` parameter in the header of `main/lvgl-file/fonts/ui_font_Font1.c` (`--bpp 1 --size 20 --font ...simkai.ttf -r 0x20-0x7f --symbols 登录注册成功失败界面...`). It only contains "the Chinese characters listed in the command + ASCII 0x20–0x7f", and **contains neither symbol glyphs such as `LV_SYMBOL_CLOSE` nor any new copy Chinese characters not listed in the command**. Therefore:

- Set with `LV_PART_MAIN` → Chinese normal, `×` missing;
- Set with `LV_PART_ITEMS` → default font's `×` normal, Chinese missing;
- Any **newly added Chinese copy** (e.g., later added "Rename", "Screen Brightness", "Clear Log") will inevitably be garbled unless the font is regenerated.

**Solution**:

1. Set message box fonts in **three places**: `lv_msgbox_get_title()` and `lv_msgbox_get_text()` use `&ui_font_Font1`; `lv_msgbox_get_btns()` uses `LV_FONT_DEFAULT` (or `&lv_font_montserrat_14`, provided that font is enabled)—**this is the suggestion given during DeepSeek troubleshooting, but the current code `main/lvgl-file/ui.c:112` still uses `&ui_font_Font1` for btns**, so the missing "×" issue theoretically still exists and is an **incompletely implemented** item;
2. Every time a Chinese UI string is added, the font **must be regenerated** (add the new Chinese characters to `--symbols` in the font conversion command), otherwise the string will definitely be garbled. It is recommended to document "change copy → regenerate font" as a fixed workflow in the README to avoid a fourth recurrence;
3. As a reminder: the header of `ui_font_Font1.c` already lists the two characters "亮度" (brightness), but the Settings page has no brightness item, indicating the font was once prepared for a brightness feature (see Section 8, suggestion 4).

**Current status**: Partially fixed (title/body fonts are set separately; the button matrix still uses the Chinese font, and the `×` display issue remains to be verified).

### 5: Unchecking "Remember Password" Still Auto-Fills Account and Password

**Symptom**: Previously checked "Remember Password" and successfully logged in once; afterward uncheck "Remember Password"; the next time entering the login page (whether on boot or returning from desktop), **the account and password are still automatically filled in, and the checkbox becomes checked again**.

**Cause analysis**: `ui_Screen1_screen_init()` is **executed every time the login page is entered** (including first boot and returning from other screens). Its logic is "as long as `read_text_from_nvs("acc"/"pwd")` succeeds, fill in the fields and call `lv_obj_add_state(ui_jizhu, LV_STATE_CHECKED)`". The uncheck branch in `ui_event_jizhu` originally **only set `is_remeber` to false and did not delete the NVS keys**, so old credentials still existed, and the next initialization filled them in and rechecked the box as before. Judging the `ui_jizhu` state during initialization does not work—it cannot distinguish "user actively unchecked" from "never saved".

**Solution**: In the `LV_EVENT_VALUE_CHANGED` branch of `ui_event_jizhu`, when `is_remeber == false`, immediately call `delete_save_account("acc")` and `delete_save_account("pwd")` (internally executing `nvs_erase_key` + `nvs_commit`).

**Additional note**: `delete_account_from_file()` in `ui_Screen4.c` also unconditionally calls the same pair of deletions, meaning "deleting any account" will also clear the remembered-password credentials. Whether this behavior is reasonable needs confirmation; if unnecessary, it is recommended to change it to "clear only when the deleted account equals the remembered account".

**Current status**: Fixed.

### 6: Repeatedly Entering and Exiting the Drawing App Causes Memory Leaks and Timer Accumulation

**Symptom**: After repeatedly "enter Drawing → exit → enter again", PSRAM/internal RAM continuously decreases; the number of time-refresh timers in the upper-right corner of the desktop keeps increasing.

**Cause analysis**: Two issues combine:

1. `ui_Screen6_screen_destroy()` **does not call `lv_obj_del(ui_Screen6)`**; it only sets pointers such as `ui_Screen6` to NULL and calls `free(canvas_buf)`. That is, every time the drawing page is exited, **the entire Screen6 object tree (including the Canvas object, palette buttons, and color `user_data` allocated by `lv_mem_alloc`) is left in LVGL's object heap**, while the Canvas internally still holds a pointer to the already-`free`d buffer (dangling reference).
2. The drawing page exit flow calls `ui_Screen3_screen_init()`, but Screen3 **was already created** during `ui_init()`. This call **creates an entire new Screen3 and overwrites the global pointer**; the old Screen3 and its `time_refresh_timer`, `ui_desktop_kb` (`lv_keyboard_create(lv_layer_top())`) all lose references; thus every time the drawing page is entered and exited, one more 1-second timer runs in the background and one more top-layer keyboard object accumulates.

**Solution**: Adopt "truly destroy + create on demand", with three specific changes.

1. Add `lv_obj_del(ui_Screen6)` in the destroy function, and adjust the order to "first delete the object tree, then free the canvas buffer" to avoid a dangling pointer in between; also clean up global states such as the selected button during destruction.
2. Change the exit to "initialize only when `ui_Screen3 == NULL`", and reuse it when it already exists; still use the animation-free `lv_scr_load()` for immediate switching (instead of `_ui_screen_change()` with a 200 ms fade-in), because the switch completes synchronously and immediately destroying the old page will not mistakenly delete objects still referenced by the animation.
3. Change palette color values to a static color table; the color buttons' `user_data` directly points to elements in the table instead of allocating each one with `lv_mem_alloc`, eliminating this leak at the source.

**Current status**: Fixed.

### 7: Drawing Save Succeeds Only Once, and Exit No Longer Prompts Unsaved Changes

**Symptom**: The first drawing save works normally; afterward, clicking save again has no response and no result prompt; after that, exiting the drawing page no longer shows the "discard changes?" confirmation box.

**Cause analysis**: The save-control state flag was used with two meanings—both "currently saving" and, in the exit logic, "canvas already saved". The flag was not reset when the background save task ended (the corresponding code was commented out), so after the first save it remained at "saving", causing two chained problems: the save button's duplicate-save check returned directly, silently ignoring subsequent saves; and the exit button always took the "saving" branch and returned directly to the desktop, completely skipping the unsaved-confirmation flow.

**Solution**:

1. Reset the flag when the background save task ends, and place it outside the mutex, ensuring it can also be reset when the UI has been destroyed;
2. Add an independent "canvas has unsaved changes" flag: set when drawing strokes and clearing the canvas, cleared after a successful save, and set again on write failure; the exit confirmation box now checks this flag;
3. During saving, complete the canvas snapshot before clearing the flag, ensuring that "clearing" only happens on the foreground thread, avoiding the background task mistakenly marking newly drawn content as saved (this race at most causes the user to see the confirmation box once more; no data is lost);
4. The drawing page's return-to-desktop logic is consolidated into a single exit.

**Current status**: Fixed.

### 8: System Stutters for About 5 Seconds When Wi-Fi Disconnects

**Symptom**: At the moment Wi-Fi disconnects, the UI and other network events show delayed responses for about 5 seconds; during this period, dragging icons and switching pages are noticeably sluggish.

**Cause analysis**: The disconnect event callback directly executed "delay 5 seconds then reconnect". This callback runs on the system event task created by `esp_event_loop_create_default()`, and delaying inside it freezes the entire event loop, so all events during that period (IP events, subsequent Wi-Fi events, and events from other components) cannot be dispatched.

**Solution**:

- The disconnect callback now only sets a "pending reconnection" event flag and returns immediately, without any blocking operation;
- The Wi-Fi background task blocks waiting for this flag, and performs the 5-second delay and reconnection call in its own task context;
- At the same time, replace the task's original busy-wait keep-alive loop with the above waiting logic, avoiding meaningless periodic wake-ups;
- The event group is created before the task starts, ensuring the order "event group is created before event callbacks are registered".

**Current status**: Fixed.

### 9: After Sliding on an Icon, Long Press Still Pops Up the Menu

**Symptom**: The intention is "only long-pressing in place pops up the menu", but after sliding a distance on a file icon, stopping, and holding, the "Delete/Rename" menu still pops up; an unselected icon also mistakenly enters drag mode after sliding and long-pressing. In addition, sliding on a selected icon and releasing directly opens the file.

**Cause analysis**: This is determined by LVGL's input event mechanism, not a wrong condition. Looking at LVGL 8.3's `lv_indev.c`:

- `LV_EVENT_LONG_PRESSED` is only emitted when "there is no scrolling object" and the press exceeds the long-press time (around lines 919–931), and **does not check movement at all**;
- `LV_EVENT_CLICKED` is likewise only emitted when "there is no scrolling object" (around lines 973–981), and **also does not check movement**.

In this project, both the desktop container and the icon container have `LV_OBJ_FLAG_SCROLLABLE` cleared (so that "long press in place" can trigger stably), so the scrolling object is always empty, and both events are emitted as usual after sliding. The desktop blank area had previously been fixed by "recording press movement", but the file icon callback set missed the same handling.

**Solution**:

1. Continuously record the maximum finger movement during a single press. The statistics code must be placed **before** the early return for "whether to enter drag mode"; otherwise, movement will never be recorded in sliding scenarios;
2. In the long-press callback, if movement exceeds a small threshold (2 pixels, consistent with the desktop blank area), return directly: do not pop up the menu, do not enter drag mode, and mark "long press already handled" to block the subsequent click;
3. In the click callback, if movement exceeds a looser threshold (12 pixels), ignore this click, avoiding "sliding on a selected icon then releasing" being treated as opening the file; the looser threshold is to prevent slight finger jitter from swallowing normal taps.

**Current status**: Fixed.

---

## Known Issues List

| # | Issue | Severity | Location | Affected Assessment Item |
| --- | --- | --- | --- | --- |
| 1 | Message box button matrix font still Chinese subset (`×` may not display) | 🟡 Low | `main/lvgl-file/ui.c:112` | Appearance |
| 2 | `delete_msgbox_cb` still uses `lv_obj_get_parent()` | 🟡 Low | `ui_Screen3.c:1277` | Stability risk |
| 3 | Desktop clock shows 1970 before sync | 🟡 Low | `ui_Screen3.c:1329` | Appearance / status display |
| 4 | `wifi_sync_get_time_str()` / `wifi_sync_is_connected()` are dead code | 🟡 Low | `main/wifi_sync.c:140,145` | Code cleanliness |
| 5 | After registration, all account passwords are printed in plaintext | 🟡 Low | `ui_Screen2.c:94` | Security |
| 6 | Buzzer prompt blocks LVGL task for 100 ms | 🟡 Low | `main/buzzer.c:108` | Performance |
| 7 | Volume slider continues buzzing if RELEASED is not received | 🟡 Low | `ui_Screen5.c:103-109` | Edge case |
| 8 | `save_draw` task priority (5) is higher than LVGL task (2) and not core-pinned | 🟡 Low | `ui_Screen6.c:327` | Residual flicker risk |
| 9 | Snapshot `malloc(307KB)` does not use PSRAM attribute | 🟡 Low | `ui_Screen6.c:316` | Out-of-memory risk |
| 10 | `WIFI_SSID`/`WIFI_PASSWORD` hardcoded in header | 🟡 Low | `main/wifi_sync.h:13-14` | Portability |
| 11 | Administrator password is a fixed constant in the header file, and the number of failed verification attempts is not limited | 🟡 Low | `ui_Screen1.c` | Security |

---

## Future Update Plan

1. **Add a system monitor app**: use `esp_timer_get_time()` for uptime, `xTaskGetTickCount()`, `uxTaskGetSystemState()` for task stack high-water marks, `esp_get_free_heap_size()` / `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` for remaining memory, plus two global counters "input event count/error count", and make a list page. It can also serve as the data source for a 60-minute stability test.
2. **Add a log app**: define `log_add(event)` to write "login failure/file creation/file deletion/settings change/drawing save" to `/spiffs/syslog.txt` (ring overwrite); the log page reads in a `read_user_file` style + a "Clear" button. It can share an "app page" template with the monitor app.
3. **Add screen brightness**: currently the backlight is a digital IO switch on CH422G and cannot dim. To satisfy this requirement, confirm whether the backlight enable pin can be changed to LEDC PWM output on the hardware; if not feasible, clearly state in the documentation "hardware limitation, replaced by manual/timeout screen-off" rather than leaving it blank.
4. **Add fake OTA**: make a "System Update" page showing the version number (can first be a string constant), simulate download progress after clicking "Check for Updates", and write a new version marker in SPIFFS; after reboot the version number changes and a new app icon is unlocked—satisfying "fake OTA + observable difference after upgrade".