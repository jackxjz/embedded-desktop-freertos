# ESP32-Based Simple FreeRTOS Operating System

[中文](README.md) | English

This project is a FreeRTOS desktop simulator running on the ESP32-S3 development board, integrating a graphical user interface (LVGL), file system, network synchronization, power management, and other core functionalities. It is designed to demonstrate the design capability of embedded multitasking systems and interactive applications.

This project is the work for the **2026 Electronic Technology Association Embedded Group Assessment**.

---

## Hardware List

### Main Controller Board

![ESP32-S3-Touch-LCD-7 Onboard Resources](https://docs.waveshare.net/assets/images/ESP32-S3-Touch-LCD-7-Intro1-1354f76103c5429920a42f7a5ca1dc7d.webp)

| Item           | Specifications                                                       |
| -------------- | -------------------------------------------------------------------- |
| Model          | ESP32-S3-Touch-LCD-7                                                 |
| Manufacturer   | Waveshare                                                            |
| Processor      | High-performance Xtensa 32-bit LX7 dual-core processor, up to 240 MHz |
| Wireless       | Supports 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5 (LE), onboard antenna |
| Flash          | 8 MB                                                                 |
| PSRAM          | 8 MB                                                                 |
| Display        | 7-inch capacitive touch screen                                       |
| Peripheral I/O | CAN, RS485, I²C, USB, etc.                                           |
| Development    | ESP-IDF                                                              |

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