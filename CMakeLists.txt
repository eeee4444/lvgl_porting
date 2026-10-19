# 有关构建系统的更多信息，请参见
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# 以下几行样板代码必须位于工程的
# CMakeLists 中，并且保持这个精确顺序，cmake 才能正常工作
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options("-Wno-attributes")
project(lvgl_porting)
