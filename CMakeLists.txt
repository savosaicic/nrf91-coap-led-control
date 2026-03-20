cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(nrf91_coap_led_control)

target_sources(app PRIVATE
  src/main.c
)

if(CONFIG_LTE_LINK_CONTROL)
  target_sources(app PRIVATE src/modem.c)
endif()

target_include_directories(app PRIVATE
  include
)
