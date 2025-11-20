set(src
    "src/mm_app_regdb.c"
    "src/scan.c")

idf_component_register(SRCS ${src} PRIV_REQUIRES driver morselib mm_shims mmipal esp_http_server)
