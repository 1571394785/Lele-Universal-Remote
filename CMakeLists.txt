# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Only build main and the components it requires. This keeps the BLE keyboard
# project from compiling unrelated ESP-IDF components such as esp_lcd.
idf_build_set_property(MINIMAL_BUILD ON)
project(remote)
