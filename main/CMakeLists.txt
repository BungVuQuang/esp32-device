# set(pri_req downloadfile esp_adc_cal untils crc_uart FtpClient mqtt port_min min host_data host_bootloader esp_uart nvs_flash esp_uart bootloader_command json_generator json_parser)
# set(app_src main.c ds18b20.c)
# idf_component_register(SRCS ${app_src}
# INCLUDE_DIRS "."
# REQUIRES ${pri_req}
# )

set(COMPONENT_SRCS ds18b20.c main.c)
set(COMPONENT_ADD_INCLUDEDIRS ".")

register_component()