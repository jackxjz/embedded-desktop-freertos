# ESP32-Based Simple FreeRTOS Operating System

[中文](README.md) | English

This project is a FreeRTOS desktop simulator running on the ESP32-S3 development board, integrating a graphical user interface (LVGL), file system, network synchronization, power management, and other core functionalities. It is designed to demonstrate the design capability of embedded multitasking systems and interactive applications.

This project is the work for the **2026 Electronic Technology Association Embedded Group Assessment**.

---

## Hardware List

### Main Controller Board

![ESP32-S3-Touch-LCD-7 Onboard Resources](https://docs.waveshare.net/assets/images/ESP32-S3-Touch-LCD-7-Intro1-1354f76103c5429920a42f7a5ca1dc7d.webp)

| Item           | Specifications                                                              |
| -------------- | --------------------------------------------------------------------------- |
| Model          | ESP32-S3-Touch-LCD-7                                                        |
| Manufacturer   | Waveshare                                                                   |
| Processor      | High-performance Xtensa 32-bit LX7 dual-core processor, up to 240 MHz       |
| Wireless       | Supports 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5 (LE), onboard antenna |
| Flash          | 8 MB                                                                        |
| PSRAM          | 8 MB                                                                        |
| Display        | 7-inch capacitive touch screen                                              |
| Peripheral I/O | CAN, RS485, I²C, USB, etc.                                                  |
| Development    | ESP-IDF                                                                     |

[Schematic](https://files.waveshare.net/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7-Sch.pdf) | [ESP32-S3-Touch-LCD-7 Drawings](https://www.waveshare.net/wiki/%E6%96%87%E4%BB%B6:ESP32-S3-Touch-LCD-7.zip)

### Other Peripherals

- Passive Buzzer (FUET FUET-5020): Electromagnetic type passive buzzer, rated voltage 3V (operating range 2V–4V), driving frequency 4kHz. SMD package (5×5×2.2mm), suitable for compact embedded designs. Maximum sound pressure level up to 75dB, operating current 110mA, stable operation from -20℃ to +70℃. Used for system event tones, keypress feedback, and alert notifications.

---

## Key Features

- **Graphical Interface**: Built with LVGL, providing desktop, app icons, window manager, and software keyboard.
- **File System**: SPIFFS supports file creation, deletion, editing, and uniqueness validation.
- **Account Management**: Local account registration/login, password error lockout mechanism, and "remember password" (encrypted storage in NVS).
- **Network Synchronization**: Auto Wi-Fi connection, NTP time synchronization, and auto-reconnect.
- **Power Management**: Auto screen-off after timeout + manual screen-off button; configurable timeout (10s / 20s / 30s).
- **System Settings**: Cursor size adjustment, NVS persistence for settings, instant effect.
- **Multitasking Scheduling**: FreeRTOS dual-core task division (UI response, network background, power monitoring, etc.).

---

## Development Log

All software changes are recorded here.

### v4.2

#### Improved

- Fixed dangling pointer issue in account management: reset selected state on list reload to prevent crashes from operating on destroyed objects
- Account list now stores account names via `user_data` instead of parsing button text, improving data access stability
- Properly free `user_data` dynamic memory when clearing or destroying the account list, eliminating memory leak risks
- Fixed message box object retrieval in file editor: use `lv_event_get_current_target()` instead of `lv_obj_get_parent()` to prevent invalid memory access and system reboot when closing unsaved files
- Fixed inconsistent remember password state: synchronize `is_remeber` flag after successful login to ensure UI state matches NVS stored credentials

### v4.1

#### Added

- Drawing app now detects unsaved changes on exit and prompts for confirmation, preventing accidental data loss
- Added buzzer feedback when closing unsaved files

#### Improved

- Drawing storage format upgraded from text to binary, with magic number (`DRAW`) and dimension validation in file header for improved data integrity and parsing speed
- 8KB batch buffer for Flash writes, reducing write cycles and extending Flash lifespan
- Auto-clear saved credentials from NVS when "Remember Password" is unchecked, preventing credential residue
- Adjusted Wi-Fi configuration structure for optimized connection parameter management

### v4.0

#### Added

- Brand new Drawing app, accessible from the desktop icon
- 480×320 LVGL Canvas centered on screen for freehand drawing
- Continuous touch drawing: point on press, line on drag, stop on release for smooth painting experience
- Drawing export and save to SPIFFS with non-white pixel compression for storage efficiency
- Auto-load saved drawing on app launch, enabling power-off recovery
- Clear canvas function: reset to white background and delete stored file

#### Improved

- Canvas buffer preferentially allocated from PSRAM (8MB) to reduce internal RAM usage and improve large canvas stability
- Graceful error handling with user-friendly prompt when memory allocation fails
- Automatic canvas buffer release on app exit to prevent memory leaks

### v3.3

#### Added

- Added passive buzzer hardware adaptation to provide system sound feedback
- Added a volume adjustment slider in the Settings app for controlling system prompt tone volume
- The buzzer continuously sounds while dragging the volume slider, allowing users to perceive the current volume level in real time for intuitive feedback
- Added buzzer beep alerts for password errors, account deletion, and unsaved file warnings to enhance interaction feedback
- Added a file rename option in the file manager, allowing modification of existing file names

#### Improved

- Optimized UI prompt wording, uniformly correcting "息屏" to "熄屏" for more accurate display terminology
- Removed duplicate NVS initialization in the Wi-Fi time sync module, now managed uniformly in the main entry to avoid redundant initialization

### v3.2

#### Added

- Added an active screen-off button on the desktop, allowing users to manually trigger instant screen-off.
- Added configurable screen-off timeout in the Settings app: 10s / 20s / 30s options.

#### Improved

- Refactored the power management module to support coexistence of auto-timeout screen-off and manual screen-off modes.
- Fixed the font application logic in message boxes: switched from global style to separate font settings for title, content, and buttons to ensure correct rendering of Chinese characters.

### v3.1

#### Added

- Integrated Wi-Fi connection capability to connect to a preset 2.4GHz wireless network.
- Automatic time synchronization via Network Time Protocol (NTP), replacing the RTC hardware solution.

#### Improved

- Automatically refresh the desktop status bar time display after NTP synchronization.
- Optimized Settings UI interaction: parameter changes are instantly written to NVS and take effect in real time.
- Auto-reconnect on network disconnection; after successful reconnection, time is automatically resynchronized.

### v3.0

#### Added

- New system Settings app, accessible from the desktop icon.
- Cursor size adjustment with multiple levels, real-time preview, and instant effect.
- Persistent storage of settings to NVS, ensuring user preferences are retained after reboot.

#### Improved

- Refactored system parameter management architecture, providing a unified NVS read/write interface for all modules.
- Optimized desktop app launch process, supporting application-level page navigation and back stack management.

### v2.2

#### Added

- Implemented auto screen-off after 10 seconds of user inactivity.
- Supports touch wake-up; the desktop restores to a reasonable state after wake.

### v2.1

#### Added

- Added a mouse cursor that tracks touch position movements.
- Added unsaved file warning dialog: when attempting to close a modified file, a confirmation popup appears.

#### Improved

- Implemented file uniqueness validation to prevent creating files with duplicate names.

### v2.0

#### Added

- New desktop main interface with app icon rendering.
- File management (create/delete files) based on SPIFFS file system.
- Desktop supports file selection and opening; file editor page supports exit and return to desktop.
- File editor supports basic text input and software keyboard invocation.

### v1.3

#### Added

- Added password error feedback: lockout for 10 seconds after 3 consecutive incorrect attempts.
- Displays a countdown timer during the lockout period to enhance user feedback.

### v1.2

#### Added

- Added user account management page.
- Implemented account list page supporting viewing and deleting registered local accounts.
- Supports account-level operations: delete account.

#### Improved

- Optimized error prompts for username/password: ensure popup notifications for wrong username/password or empty password.
- Enhanced input focus management and software keyboard adaptation on the login page.

### v1.1

#### Added

- Added user terms confirmation option in the login flow.
- Added "Remember Password" function in the login form, with credentials encrypted and stored in NVS.
- Integrated SPIFFS lightweight file system to support persistent storage of account data.

#### Improved

- Optimized registration flow: added username uniqueness validation, real-time conflict prompt, and prevention of duplicate registration.

### v1.0

#### Added

- Initial project release, built on ESP32-S3 + FreeRTOS + LVGL.
- Implemented system login and registration page architecture.
- Basic password validation engine supporting local account registration and login authentication.
- Implemented BSP layer abstraction: LCD display driver, touch controller, backlight control.
- Basic GUI framework: page switching, popups, buttons, software keyboard, and other common components.

---

## Troubleshooting

### Screen Flickering When Saving Drawing

**Phenomenon**: When clicking the save button in the drawing app, the screen flickers noticeably; the more pixels drawn, the more severe and longer the flickering lasts.

**Cause Analysis**:

1. **Main thread blocking**: The `drawing_save()` function executes the save logic directly in the LVGL main thread (event callback), iterating over 480×320 = 153,600 pixels and calling `fprintf` for every non‑white pixel. When the drawing contains many pixels (tens of thousands), tens of thousands of file system calls can block the main thread for hundreds of milliseconds or even seconds.
2. **Flash write overhead**: Writing to SPIFFS may trigger cache misses, further increasing CPU stalls; at the same time, LVGL refresh depends on VSYNC or timers. Blocking the main thread interrupts the refresh, and the catch‑up refresh after the block causes visual flickering.

**Solutions**:

- Move the save operation to a **FreeRTOS background task** to avoid blocking the LVGL main thread. The dual‑core ESP32‑S3 can handle UI rendering and file writing separately.
- Use an **8 KB memory buffer for batch writing**; write to Flash only when the buffer is full using `fwrite`, greatly reducing the number of system calls.
- Show a "Saving…" overlay during the save process to prevent user interaction; after saving completes, safely call back to the UI via `lvgl_port_lock` to display the result.
- (Optional) Adopt a **binary storage format** (6 bytes per pixel, containing x/y coordinates and colour value), reducing data size to about one‑third of the text format for faster writing.

### Crashes Related to File Manager

- **Crash when refreshing account list**: The selected state was not reset during list refresh, leading to dangling pointer access when operating on destroyed objects. Fix: reset the selected state upon refresh (see v4.2 improvements).
- **Crash when closing file editor**: In the message box callback, using `lv_obj_get_parent()` incorrectly retrieved the object hierarchy, causing access to an invalid address. Fix: use `lv_event_get_current_target()` to get the current event target (see v4.2 improvements).
- **Memory leak**: The `user_data` dynamic memory was not released when clearing or destroying the account list. Fix: properly free `user_data` (see v4.2 improvements).

### Inconsistent "Remember Password" State

**Phenomenon**: The checkbox state of "Remember Password" on the login page does not match the credentials actually stored in NVS.

**Cause and Fix**: The `is_remeber` state variable was not updated after successful login, causing inconsistency between the page state and stored data. Fix: synchronise the state variable after login (see v4.2 improvements). Additionally, when unchecking "Remember Password", the saved credentials in NVS are automatically cleared (see v4.1 improvements).

### System Settings Not Taking Effect

**Phenomenon**: After modifying settings such as cursor size or screen‑off timeout, the configuration is lost after reboot or the changes do not take effect immediately.

**Cause and Fix**: Parameters were not persisted to NVS, or the relevant modules were not notified to refresh after changes. Fix: unify the NVS read/write interface; write parameters to NVS immediately upon change and broadcast refresh events (see v3.1 and v3.0 improvements).

### Network Time Synchronisation Failure

**Phenomenon**: Time is not synchronised after Wi‑Fi connects successfully, or time is not updated after reconnection.

**Cause and Fix**: The desktop status bar time display was not refreshed after NTP synchronisation; also, automatic re‑synchronisation was not triggered after reconnection. Fix: actively refresh the display after synchronisation; implement automatic reconnection and auto‑synchronisation upon reconnection (see v3.1 improvements).
