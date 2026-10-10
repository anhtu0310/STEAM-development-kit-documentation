idf_component_register(
    SRCS
        "main.cpp"
        "app.cpp"
        "vga4.cpp"
        "gfx.cpp"
        "scene.cpp"
        "peripherals.cpp"
        "oled.cpp"
        "input.cpp"
        "usb_keyboard.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_ledc
        esp_driver_rmt
        esp_driver_uart
        esp_psram
        esp_timer
        spi_flash
        usb
        usb_host_hid
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter)
