/*
 * Copyright 2022-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Example application to demonstrate Wi-Fi HaLow scan, connect, DHCP, and HTTP server.
 *
 * This example:
 * - Scans for Wi-Fi HaLow (802.11ah) access points
 * - Connects to a specified AP using app_wlan_init()/app_wlan_start()
 * - Obtains an IP address via DHCP
 * - Starts an HTTP web server accessible from the network
 *
 * @note It is assumed that you have followed the steps in the @ref GETTING_STARTED guide and are
 * therefore familiar with how to build, flash, and monitor an application using the MM-IoT-SDK
 * framework.
 */

#include <endian.h>
#include <string.h>
#include "mmhal.h"
#include "mmosal.h"
#include "mmwlan.h"
#include "mmipal.h"
#include "mm_app_common.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_event.h"
#include "driver/gpio.h"

/** Onboard LED GPIO pin (inverted: LOW=ON, HIGH=OFF) */
#define LED_GPIO 21


/*
 * If ASNI_ESCAPE_ENABLED is non-zero (the default) then ANSI escape characters will be used to
 *  format the log output.
 */
#if !(defined(ASNI_ESCAPE_ENABLED) && ASNI_ESCAPE_ENABLED == 0)
/** ANSI escape sequence for bold text. */
#define ANSI_BOLD  "\x1b[1m"
/** ANSI escape sequence to reset font. */
#define ANSI_RESET "\x1b[0m"
#else
/** ANSI escape sequence for bold text (disabled so no-op). */
#define ANSI_BOLD  ""
/** ANSI escape sequence to reset font (disabled so no-op). */
#define ANSI_RESET ""
#endif

/** Length of string representation of a MAC address (i.e., "XX:XX:XX:XX:XX:XX")
 * including null terminator. */
#define MAC_ADDR_STR_LEN    (18)

/** Number of results found. */
static int num_scan_results;

/** Flag to indicate scan completion. */
static bool scan_completed = false;

/** Enumeration of Authentication Key Management (AKM) Suite OUIs as BE32 integers. */
enum akm_suite_oui
{
    /** Open (no security) */
    AKM_SUITE_NONE = 0,
    /** Pre-shared key (WFA OUI) */
    AKM_SUITE_PSK = 0x506f9a02,
    /** Simultaneous Authentication of Equals (SAE) */
    AKM_SUITE_SAE = 0x000fac08,
    /** OWE */
    AKM_SUITE_OWE = 0x000fac12,
    /** Another suite not in this enum */
    AKM_SUITE_OTHER = 1,
};

/**
 * Get the name of the given AKM Suite as a string.
 *
 * @param akm_suite_oui     The OUI of the AKM suite as a big endian integer.
 *
 * @returns the string representation.
 */
const char *akm_suite_to_string(uint32_t akm_suite_oui)
{
    switch (akm_suite_oui)
    {
    case AKM_SUITE_NONE:
        return "None";

    case AKM_SUITE_PSK:
        return "PSK";

    case AKM_SUITE_SAE:
        return "SAE";

    case AKM_SUITE_OWE:
        return "OWE";

    default:
        return "Other";
    }
}

/** Maximum number of pairwise cipher suites our parser will process. */
#define RSN_INFORMATION_MAX_PAIRWISE_CIPHER_SUITES  (2)

/** Maximum number of AKM suites our parser will process. */
#define RSN_INFORMATION_MAX_AKM_SUITES  (2)


/**
 * Data structure to represent information extracted from an RSN information element.
 *
 * All integers in host order.
 */
struct rsn_information
{
    /** The group cipher suite OUI. */
    uint32_t group_cipher_suite;
    /** Pairwise cipher suite OUIs. Count given by @c num_pairwise_cipher_suites. */
    uint32_t pairwise_cipher_suites[RSN_INFORMATION_MAX_PAIRWISE_CIPHER_SUITES];
    /** AKM suite OUIs. Count given by @c num_akm_suites. */
    uint32_t akm_suites[RSN_INFORMATION_MAX_AKM_SUITES];
    /** Number of pairwise cipher suites in @c pairwise_cipher_suites. */
    uint16_t num_pairwise_cipher_suites;
    /** Number of AKM suites in @c akm_suites. */
    uint16_t num_akm_suites;
    /** Version number of the RSN IE. */
    uint16_t version;
    /** RSN Capabilities field of the RSN IE (in host order). */
    uint16_t rsn_capabilities;
};


/** Tag number of the RSN information element, in which we can find security details of the AP. */
#define RSN_INFORMATION_IE_TYPE (48)

/**
 * Search through the given list of information elements to find the RSN IE then parse it
 * to extract relevant information into an instance of @ref rsn_information.
 *
 * @param[in] ies       Buffer containing the information elements.
 * @param[in] ies_len   Length of @p ies
 * @param[out] output   Pointer to an instance of @ref rsn_information to receive output.
 *
 * @returns -1 on parse error, 0 if the RSN IE was not found, 1 if the RSN IE was found.
 */
static int parse_rsn_information(const uint8_t *ies, unsigned ies_len,
                                 struct rsn_information *output)
{
    size_t offset = 0;
    memset(output, 0, sizeof(*output));

    while (offset < ies_len)
    {
        uint8_t type = ies[offset++];
        uint8_t length = ies[offset++];

        if (type == RSN_INFORMATION_IE_TYPE)
        {
            uint16_t num_pairwise_cipher_suites;
            uint16_t num_akm_suites;
            uint16_t ii;


            if (offset + length > ies_len)
            {
                printf("*WRN* RSN IE extends past end of IEs\n");
                return -1;
            }

            if (length < 8)
            {
                printf("*WRN* RSN IE too short\n");
                return -1;
            }

            /* Skip version field */
            output->version = ies[offset] | ies[offset+1] << 8;
            offset += 2;
            length -= 2;

            output->group_cipher_suite =
                ies[offset] << 24 | ies[offset+1] << 16 | ies[offset+2] << 8 | ies[offset+3];
            offset += 4;
            length -= 4;

            num_pairwise_cipher_suites = ies[offset] | ies[offset+1] << 8;
            offset += 2;
            length -= 2;

            output->num_pairwise_cipher_suites = num_pairwise_cipher_suites;
            if (num_pairwise_cipher_suites > RSN_INFORMATION_MAX_PAIRWISE_CIPHER_SUITES)
            {
                output->num_pairwise_cipher_suites = RSN_INFORMATION_MAX_PAIRWISE_CIPHER_SUITES;
            }

            if (length < 4 * num_pairwise_cipher_suites + 2)
            {
                printf("*WRN* RSN IE too short\n");
                return -1;
            }

            for (ii = 0; ii < num_pairwise_cipher_suites; ii++)
            {
                if (ii < output->num_pairwise_cipher_suites)
                {
                    output->pairwise_cipher_suites[ii] =
                        ies[offset] << 24 | ies[offset+1] << 16 |
                        ies[offset+2] << 8 | ies[offset+3];
                }
                offset += 4;
                length -= 4;
            }

            num_akm_suites = ies[offset] | ies[offset+1] << 8;
            offset += 2;
            length -= 2;

            output->num_akm_suites = num_akm_suites;
            if (num_akm_suites > RSN_INFORMATION_MAX_AKM_SUITES)
            {
                output->num_akm_suites = RSN_INFORMATION_MAX_AKM_SUITES;
            }

            if (length < 4 * num_akm_suites + 2)
            {
                printf("*WRN* RSN IE too short\n");
                return -1;
            }

            for (ii = 0; ii < num_akm_suites; ii++)
            {
                if (ii < output->num_akm_suites)
                {
                    output->akm_suites[ii] =
                        ies[offset] << 24 | ies[offset+1] << 16 |
                        ies[offset+2] << 8 | ies[offset+3];
                }
                offset += 4;
                length -= 4;
            }

            output->rsn_capabilities = ies[offset] | ies[offset+1] << 8;
            return 1;
        }

        offset += length;
    }

    /* No RSE IE found; implies open security. */
    return 0;
}


/**
 * Scan rx callback.
 *
 * @param result        Pointer to the scan result.
 * @param arg           Opaque argument.
 */
static void scan_rx_callback(const struct mmwlan_scan_result *result, void *arg)
{
    (void)(arg);
    char bssid_str[MAC_ADDR_STR_LEN];
    char ssid_str[MMWLAN_SSID_MAXLEN];
    int ret;
    struct rsn_information rsn_info;

    num_scan_results++;
    snprintf(bssid_str, MAC_ADDR_STR_LEN, "%02x:%02x:%02x:%02x:%02x:%02x",
             result->bssid[0], result->bssid[1], result->bssid[2], result->bssid[3],
             result->bssid[4], result->bssid[5]);
    snprintf(ssid_str, (result->ssid_len+1), "%s", result->ssid);

    printf(ANSI_BOLD "%2d. %s" ANSI_RESET "\n", num_scan_results, ssid_str);
    printf("    Operating BW: %u MHz\n",  result->op_bw_mhz);
    printf("    BSSID: %s\n", bssid_str);
    printf("    RSSI: %3d\n", result->rssi);
    printf("    Beacon Interval(TUs): %u\n", result->beacon_interval);
    printf("    Capability Info: 0x%04x\n", result->capability_info);

    ret = parse_rsn_information(result->ies, result->ies_len, &rsn_info);
    if (ret < 0)
    {
        printf("    Invalid probe response\n");
    }
    else if (rsn_info.num_akm_suites == 0)
    {
        printf("    Security: None\n");
    }
    else if (ret > 0)
    {
        unsigned ii;
        printf("    Security:");
        for (ii = 0; ii < rsn_info.num_akm_suites; ii++)
        {
            printf(" %s", akm_suite_to_string(rsn_info.akm_suites[ii]));
        }
        printf("\n");
    }
}

/**
 * Scan complete callback.
 *
 * @param state         Scan complete status.
 * @param arg           Opaque argument.
 */
static void scan_complete_callback(enum mmwlan_scan_state state, void *arg)
{
    (void)(state);
    (void)(arg);
    printf("Scanning completed.\n");
    scan_completed = true;
}


/** HTTP server instance */
static httpd_handle_t server = NULL;

/** TAG for logging */
static const char *TAG = "halow_webserver";

/** LED state (true = ON, false = OFF) */
static bool led_state = false;

/**
 * Initialize the onboard LED
 */
static void led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Turn LED off initially (HIGH = OFF because inverted) */
    gpio_set_level(LED_GPIO, 1);
    led_state = false;

    ESP_LOGI(TAG, "LED initialized on GPIO%d", LED_GPIO);
}

/**
 * Set LED state
 */
static void led_set(bool on)
{
    /* LED is inverted: LOW=ON, HIGH=OFF */
    gpio_set_level(LED_GPIO, on ? 0 : 1);
    led_state = on;
    ESP_LOGI(TAG, "LED turned %s", on ? "ON" : "OFF");
}

/**
 * Toggle LED state
 */
static void led_toggle(void)
{
    led_set(!led_state);
}

/**
 * Blink LED a few times (demo function)
 */
static void led_blink_demo(void)
{
    ESP_LOGI(TAG, "LED blink demo starting...");
    for (int i = 0; i < 5; i++)
    {
        led_set(true);
        mmosal_task_sleep(200);
        led_set(false);
        mmosal_task_sleep(200);
    }
    ESP_LOGI(TAG, "LED blink demo complete");
}

/**
 * HTTP GET handler for root path
 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    struct mmipal_ip_config ip_config;

    const char* html_page =
        "<!DOCTYPE html>"
        "<html><head><title>HaLow Device</title>"
        "<style>"
        "body { font-family: Arial; margin: 40px; background: #f0f0f0; }"
        "h1 { color: #333; }"
        ".info { background: white; padding: 20px; border-radius: 8px; margin: 10px 0; }"
        ".label { font-weight: bold; color: #666; }"
        ".led-status { display: inline-block; width: 20px; height: 20px; border-radius: 50%%; "
        "background: %s; border: 2px solid #333; margin-left: 10px; }"
        ".button { background: #4CAF50; border: none; color: white; padding: 15px 32px; "
        "text-align: center; text-decoration: none; display: inline-block; font-size: 16px; "
        "margin: 4px 2px; cursor: pointer; border-radius: 4px; }"
        ".button:hover { background: #45a049; }"
        ".blink-btn { background: #008CBA; }"
        ".blink-btn:hover { background: #007399; }"
        "</style></head>"
        "<body>"
        "<h1>ESP32-S3 Wi-Fi HaLow Web Server</h1>"
        "<div class='info'>"
        "<p><span class='label'>Status:</span> Connected via Wi-Fi HaLow (802.11ah)</p>"
        "<p><span class='label'>IP Address:</span> %s</p>"
        "<p><span class='label'>Netmask:</span> %s</p>"
        "<p><span class='label'>Gateway:</span> %s</p>"
        "<p><span class='label'>Chip:</span> Morse Micro MM6108A1</p>"
        "</div>"
        "<div class='info'>"
        "<h2>LED Control</h2>"
        "<p><span class='label'>LED Status:</span> %s<span class='led-status'></span></p>"
        "<button class='button' onclick='toggleLED()'>Toggle LED</button>"
        "<button class='button blink-btn' onclick='blinkLED()'>Blink Demo</button>"
        "</div>"
        "<div class='info'>"
        "<p>This device is successfully connected to the network using Sub-GHz Wi-Fi HaLow technology!</p>"
        "</div>"
        "<script>"
        "function toggleLED() { fetch('/led/toggle').then(() => location.reload()); }"
        "function blinkLED() { fetch('/led/blink').then(() => setTimeout(() => location.reload(), 2500)); }"
        "</script>"
        "</body></html>";

    /* Get current IP configuration from mmipal */
    if (mmipal_get_ip_config(&ip_config) == MMIPAL_SUCCESS)
    {
        char response[2048];
        const char *led_color = led_state ? "#00ff00" : "#ff0000";
        const char *led_text = led_state ? "ON" : "OFF";

        snprintf(response, sizeof(response), html_page,
                 led_color,  /* LED indicator color */
                 ip_config.ip_addr, ip_config.netmask, ip_config.gateway_addr,
                 led_text);  /* LED status text */
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        const char *error_msg = "Error: Could not retrieve network information";
        httpd_resp_send(req, error_msg, HTTPD_RESP_USE_STRLEN);
    }

    return ESP_OK;
}

/**
 * HTTP GET handler for LED toggle
 */
static esp_err_t led_toggle_handler(httpd_req_t *req)
{
    led_toggle();
    const char *resp = led_state ? "LED ON" : "LED OFF";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * HTTP GET handler for LED blink demo
 */
static esp_err_t led_blink_handler(httpd_req_t *req)
{
    led_blink_demo();
    httpd_resp_send(req, "Blink complete", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * URI handler structure for root path
 */
static const httpd_uri_t root_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

/**
 * URI handler for LED toggle
 */
static const httpd_uri_t led_toggle_uri = {
    .uri       = "/led/toggle",
    .method    = HTTP_GET,
    .handler   = led_toggle_handler,
    .user_ctx  = NULL
};

/**
 * URI handler for LED blink
 */
static const httpd_uri_t led_blink_uri = {
    .uri       = "/led/blink",
    .method    = HTTP_GET,
    .handler   = led_blink_handler,
    .user_ctx  = NULL
};

/**
 * Start the HTTP server
 */
static esp_err_t start_webserver(void)
{
    struct mmipal_ip_config ip_config;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 8192;  /* Increase from default 4096 to handle larger HTML */

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &led_toggle_uri);
        httpd_register_uri_handler(server, &led_blink_uri);
        ESP_LOGI(TAG, "HTTP server started successfully!");

        /* Get and display current IP address */
        if (mmipal_get_ip_config(&ip_config) == MMIPAL_SUCCESS)
        {
            ESP_LOGI(TAG, "Access the web page at: http://%s", ip_config.ip_addr);
        }

        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
}

/**
 * Main entry point to the application. This will be invoked in a thread once operating system
 * and hardware initialization has completed. It may return, but it does not have to.
 */
void app_main(void)
{
    enum mmwlan_status status;

    printf("\n\nMorse HaLow Web Server Demo (Built "__DATE__ " " __TIME__ ")\n\n");

    /* Initialize ESP event loop (required for HTTP server) */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize onboard LED */
    led_init();

    /* Blink LED to show we're starting up */
    printf("LED startup blink sequence...\n");
    led_blink_demo();

    /* Initialize WLAN and IP stack */
    printf("\n===== Initializing Wi-Fi HaLow Interface =====\n");
    app_wlan_init();

    /* CRITICAL: Disable power save mode BEFORE connecting
     * Seeed Xiao HaLow board does not wire the BUSY pin, so power save MUST be disabled */
    printf("Disabling power save mode (Seeed Xiao HaLow requirement)...\n");
    status = mmwlan_set_power_save_mode(MMWLAN_PS_DISABLED);
    if (status != MMWLAN_SUCCESS)
    {
        printf("ERROR: Failed to disable power save mode (status=%d)\n", status);
        printf("Connection will likely fail!\n");
    }
    else
    {
        printf("Power save mode disabled successfully\n");
    }

    /* Connect to Wi-Fi and obtain IP address */
    printf("\n===== Connecting to Wi-Fi =====\n");
    app_wlan_start();  /* Blocks until connected with IP address */

    printf("\n===== Successfully Connected with IP Address! =====\n");

    /* Turn on LED to indicate we're online */
    led_set(true);
    printf("LED turned ON - device is online!\n");

    /* Start HTTP web server */
    printf("\n===== Starting Web Server =====\n");
    if (start_webserver() == ESP_OK)
    {
        struct mmipal_ip_config ip_config;
        printf("\n*** Web server is running! ***\n");
        if (mmipal_get_ip_config(&ip_config) == MMIPAL_SUCCESS)
        {
            printf("*** Visit http://%s in your browser ***\n\n", ip_config.ip_addr);
        }
    }
    else
    {
        printf("Failed to start web server\n");
    }

    printf("Device is now running and serving web pages.\n");
    printf("Press Ctrl+C or reset button to exit.\n\n");

    /* Loop forever - web server handles requests in background */
    while (1)
    {
        mmosal_task_sleep(1000);
    }
}
