set(src
    "src/scan.c")

idf_component_register(SRCS ${src} PRIV_REQUIRES driver morselib mm_shims mmipal halow esp_http_server)
