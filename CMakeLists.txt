# SPDX-FileCopyrightText: 2026 BFU Electronics LTD
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-none-eabi.cmake CACHE FILEPATH "")
project(bfu_kb56_fw C)

# BOARD: BFU_KB56 (the keyboard PCB, STM32G030K6T6) or NUCLEO_G071RB (protocol test build)
set(BOARD BFU_KB56 CACHE STRING "Target board")
set_property(CACHE BOARD PROPERTY STRINGS BFU_KB56 NUCLEO_G071RB)
option(DEBUG_KEEP_SWD_IN_STOP "Keep SWD alive in Stop mode (higher sleep current)" OFF)

if(BOARD STREQUAL "BFU_KB56")
  set(TARGET bfu-kb56)
  set(MCU_DEF STM32G030xx)
  set(LDSCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/ld/stm32g030k6.ld)
elseif(BOARD STREQUAL "NUCLEO_G071RB")
  set(TARGET bfu-kb56-nucleo-g071rb)
  set(MCU_DEF STM32G071xx)
  set(LDSCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/ld/stm32g071rb.ld)
else()
  message(FATAL_ERROR "Unknown BOARD ${BOARD}")
endif()

add_executable(${TARGET}.elf
  src/app/app.c src/app/keymap.c src/app/fifo.c src/app/utf8.c
  src/hw/main.c src/hw/startup.c src/hw/boot.c src/hw/i2c_slave.c src/hw/matrix.c
  src/hw/led.c src/hw/power.c src/hw/uart_log.c src/hw/demo.c)

target_include_directories(${TARGET}.elf PRIVATE
  src/app src/hw third_party/cmsis_core/Include third_party/cmsis_device_g0/Include)

target_compile_definitions(${TARGET}.elf PRIVATE ${MCU_DEF} BOARD_${BOARD}
  $<$<BOOL:${DEBUG_KEEP_SWD_IN_STOP}>:DEBUG_KEEP_SWD_IN_STOP>)

set(CPU_FLAGS -mcpu=cortex-m0plus -mthumb)
target_compile_options(${TARGET}.elf PRIVATE ${CPU_FLAGS} -std=c11 -Os -g3
  -ffunction-sections -fdata-sections -fno-common
  -Wall -Wextra -Werror -Wshadow -Wdouble-promotion -Wundef)
target_link_options(${TARGET}.elf PRIVATE ${CPU_FLAGS} -T${LDSCRIPT} -nostartfiles
  -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections -Wl,-Map=${TARGET}.map -Wl,--print-memory-usage)

add_custom_command(TARGET ${TARGET}.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary ${TARGET}.elf ${TARGET}.bin
  COMMAND ${CMAKE_OBJCOPY} -O ihex ${TARGET}.elf ${TARGET}.hex
  COMMAND ${CMAKE_SIZE} ${TARGET}.elf)

# make flash: ST-LINK (NUCLEO on-board ST-LINK/V2-1 or any ST-LINK), connect under reset
add_custom_target(flash
  COMMAND openocd -f interface/stlink.cfg -f target/stm32g0x.cfg
          -c "reset_config srst_only srst_nogate connect_assert_srst"
          -c "program ${TARGET}.elf verify reset exit"
  DEPENDS ${TARGET}.elf USES_TERMINAL)
