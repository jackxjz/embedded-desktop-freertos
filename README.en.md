# A Simple FreeRTOS-based OS for ESP32

[中文](README.md) | English

This project is a FreeRTOS desktop OS simulator running on ESP32, developed for the 2026 Embedded Systems Group Assessment of the Electronics Technology Association.

## Hardware List

### Main Development Board

![ESP32-S3-Touch-LCD-7 Board Resources](https://docs.waveshare.net/assets/images/ESP32-S3-Touch-LCD-7-Intro1-1354f76103c5429920a42f7a5ca1dc7d.webp)

| Item | Specification |
|------|-------------|
| Model | ESP32-S3-Touch-LCD-7 |
| Manufacturer | Waveshare |
| Processor | High-performance Xtensa 32-bit LX7 dual-core processor, up to 240 MHz |
| Wireless | 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5 (LE), on-board antenna |
| Flash | 8 MB |
| PSRAM | 8 MB |
| Display | 7-inch capacitive touchscreen |
| Peripherals | CAN, RS485, I²C, USB, etc. |
| Development Framework | ESP-IDF |

[Schematic](https://files.waveshare.net/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7-Sch.pdf) | [ESP32-S3-Touch-LCD-7 Dimensional Drawing](https://www.waveshare.net/wiki/%E6%96%87%E4%BB%B6:ESP32-S3-Touch-LCD-7.zip)

### Other Peripherals

> To be added

## Development Log

All software changes are documented here.

### v2.2

#### Added
- Add idle timeout auto-sleep: screen turns off after 10 seconds of inactivity
- Support touch-to-wake: display restores to valid desktop state on touch input

### v2.1

#### Added
- Mouse cursor rendering with touch position tracking
- Unsaved file prompt: confirmation dialog when attempting to close unsaved files

#### Improved
- File uniqueness validation mechanism to prevent duplicate filenames

### v2.0

#### Added
- New desktop main interface with application icon rendering
- File management based on SPIFFS: create and delete operations
- Desktop supports file selection and opening; file editor supports exit and return to desktop

### v1.3

#### Added
- Password error feedback: lock for 10 seconds after 3 consecutive failed attempts

### v1.2

#### Added
- User account management page
- Account list view supporting viewing and deleting registered local accounts
- Account-level operations: delete account

#### Improved
- Enhanced username/password error prompts: popup notifications for incorrect credentials or empty password fields
- Improved login page input focus management and on-screen keyboard adaptation

### v1.1

#### Added
- User terms confirmation option added to login flow
- "Remember password" feature in login form with encrypted credential storage in NVS
- Integrated SPIFFS lightweight file system for persistent account data storage

#### Improved
- Optimized registration flow: real-time username uniqueness validation with conflict prevention

### v1.0

#### Added
- Initial release based on ESP32-S3 + FreeRTOS + LVGL
- System login and registration page architecture
- Basic password verification engine supporting local account registration and authentication
- BSP layer abstraction: LCD display driver, touch controller, backlight control

## Troubleshooting