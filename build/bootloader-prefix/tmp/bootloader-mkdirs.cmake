# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/esp-idf/v5.3.3/v5.3.3/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "D:/esp-idf/v5.3.3/v5.3.3/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "D:/esp32-gc/esp32-lvgllogin/ESP32-LVGLOGIN/build/bootloader"
  "D:/esp32-gc/esp32-lvgllogin/ESP32-LVGLOGIN/build/bootloader-prefix"
  "D:/esp32-gc/esp32-lvgllogin/ESP32-LVGLOGIN/build/bootloader-prefix/tmp"
  "D:/esp32-gc/esp32-lvgllogin/ESP32-LVGLOGIN/build/bootloader-prefix/src/bootloader-stamp"
  "D:/esp32-gc/esp32-lvgllogin/ESP32-LVGLOGIN/build/bootloader-prefix/src"
  "D:/esp32-gc/esp32-lvgllogin/ESP32-LVGLOGIN/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/esp32-gc/esp32-lvgllogin/ESP32-LVGLOGIN/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/esp32-gc/esp32-lvgllogin/ESP32-LVGLOGIN/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
