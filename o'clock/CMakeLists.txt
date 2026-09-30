# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(COMPONENTS main)

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../.vendor/waveshare-esp32-s3-rlcd-4.2/02_Example/ESP-IDF/11_U8G2_Test/components/u8g2"
    "${CMAKE_CURRENT_LIST_DIR}/../.vendor/waveshare-esp32-s3-rlcd-4.2/02_Example/ESP-IDF/11_U8G2_Test/components/u8g2_st7305")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(oclock)
