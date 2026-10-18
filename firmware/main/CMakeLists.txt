idf_component_register(
    SRCS "main.c" "proto.c" "radio.c" "led.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash driver
)
