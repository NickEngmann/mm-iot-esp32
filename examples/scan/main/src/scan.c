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
 * - Connects to a specified AP with SAE security
 * - Obtains an IP address via DHCP using mmipal
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
#include "mm_app_regdb.h"
#include "esp_http_server.h"
#include "esp_log.h"

#define COUNTRY_CODE "US"
#ifndef COUNTRY_CODE
#error COUNTRY_CODE must be defined to the appropriate 2 character country code. \
       See mm_app_regdb.c for valid options.
#endif

/** SSID of the AP to connect to after scan. */
#define SSID "halowlink1-31af"
/** Passphrase of the AP to connect to. */
#define PASSPHRASE "snowy80skies4carve"
/** Delay in seconds between scan completion and connection attempt. */
#define SCAN_TO_CONNECT_DELAY_S 5

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

/** Semaphore to signal link up. */
static struct mmosal_semb *link_up_semaphore = NULL;

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

/**
 * Link state callback for connection. Signals when link goes up/down.
 */
static void link_state_change_handler(enum mmwlan_link_state link_state, void *arg)
{
    printf("Link went %s\n", (link_state == MMWLAN_LINK_DOWN) ? "Down" : "Up");

    if (link_state == MMWLAN_LINK_UP)
    {
        struct mmosal_semb *semaphore = (struct mmosal_semb *)arg;
        bool ok = mmosal_semb_give(semaphore);
        if (!ok)
        {
            printf("Failed to give link_up_semaphore\n");
            MMOSAL_ASSERT(false);
        }
    }
}

/**
 * Receive callback. Invoked when a packet is received.
 */
static void rx_handler(uint8_t *header, unsigned header_len,
                       uint8_t *payload, unsigned payload_len,
                       void *arg)
{
    (void)(payload);
    (void)(payload_len);
    (void)(arg);

    struct __attribute__((packed)) dot3_header
    {
        uint8_t dest_addr[6];
        uint8_t src_addr[6];
        uint16_t ethertype;
    };

    struct dot3_header *hdr = (struct dot3_header *)header;

    MMOSAL_ASSERT(sizeof(*hdr) == header_len);

    printf("RX from %02x:%02x:%02x:%02x:%02x:%02x type 0x%04x len=%u\n",
           hdr->src_addr[0], hdr->src_addr[1], hdr->src_addr[2],
           hdr->src_addr[3], hdr->src_addr[4], hdr->src_addr[5],
           be16toh(hdr->ethertype), payload_len);
}

/**
 * STA status callback. Reports connection state changes.
 */
static void sta_status_handler(enum mmwlan_sta_state sta_state)
{
    const char *sta_state_desc[] = {
        "DISABLED",
        "CONNECTING",
        "CONNECTED",
    };
    printf("STA state: %s (%u)\n", sta_state_desc[sta_state], sta_state);
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

void app_print_version_info(void)
{
    enum mmwlan_status status;
    struct mmwlan_version version = {0};
    struct mmwlan_bcf_metadata bcf_metadata = {0};

    printf("-----------------------------------\n");

    status = mmwlan_get_bcf_metadata(&bcf_metadata);
    if (status == MMWLAN_SUCCESS)
    {
        printf("  BCF API version:         %u.%u.%u\n",
               bcf_metadata.version.major, bcf_metadata.version.minor, bcf_metadata.version.patch);
        if (bcf_metadata.build_version[0] != '\0')
        {
            printf("  BCF build version:       %s\n", bcf_metadata.build_version);
        }
        if (bcf_metadata.board_desc[0] != '\0')
        {
            printf("  BCF board description:   %s\n", bcf_metadata.board_desc);
        }
    }
    else
    {
        printf("  !! BCF metadata retrival failed !!\n");
    }

    status = mmwlan_get_version(&version);
    if (status != MMWLAN_SUCCESS)
    {
        printf("  !! Error occured whilst retrieving version info !!\n");
    }
    printf("  Morselib version:        %s\n", version.morselib_version);
    printf("  Morse firmware version:  %s\n", version.morse_fw_version);
    printf("  Morse chip ID:           0x%04lx\n", version.morse_chip_id);
    printf("  Morse chip name:         %s\n", version.morse_chip_id_string);
    printf("-----------------------------------\n");

    MMOSAL_ASSERT(status == MMWLAN_SUCCESS);
}

/** HTTP server instance */
static httpd_handle_t server = NULL;

/** IP address string */
static char ip_address[16] = "0.0.0.0";

/** TAG for logging */
static const char *TAG = "halow_webserver";

/**
 * Link status callback - called when IP address is assigned
 */
static void link_status_callback(const struct mmipal_link_status *link_status)
{
    if (link_status->link_state == MMIPAL_LINK_UP)
    {
        printf("\n===== IP Address Assigned =====\n");
        printf("  IP Address: %s\n", link_status->ip_addr);
        printf("  Netmask:    %s\n", link_status->netmask);
        printf("  Gateway:    %s\n", link_status->gateway);
        printf("===============================\n\n");

        /* Save IP address for HTTP server */
        strncpy(ip_address, link_status->ip_addr, sizeof(ip_address) - 1);
    }
    else
    {
        printf("Link is down\n");
        strncpy(ip_address, "0.0.0.0", sizeof(ip_address));
    }
}

/**
 * HTTP GET handler for root path
 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char* html_page =
        "<!DOCTYPE html>"
        "<html><head><title>HaLow Device</title>"
        "<style>"
        "body { font-family: Arial; margin: 40px; background: #f0f0f0; }"
        "h1 { color: #333; }"
        ".info { background: white; padding: 20px; border-radius: 8px; margin: 10px 0; }"
        ".label { font-weight: bold; color: #666; }"
        "</style></head>"
        "<body>"
        "<h1>ESP32-S3 Wi-Fi HaLow Web Server</h1>"
        "<div class='info'>"
        "<p><span class='label'>Status:</span> Connected via Wi-Fi HaLow (802.11ah)</p>"
        "<p><span class='label'>IP Address:</span> %s</p>"
        "<p><span class='label'>Chip:</span> Morse Micro MM6108A1</p>"
        "</div>"
        "<div class='info'>"
        "<p>This device is successfully connected to the network using Sub-GHz Wi-Fi HaLow technology!</p>"
        "</div>"
        "</body></html>";

    char response[1024];
    snprintf(response, sizeof(response), html_page, ip_address);

    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
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
 * Start the HTTP server
 */
static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &root_uri);
        ESP_LOGI(TAG, "HTTP server started successfully!");
        ESP_LOGI(TAG, "Access the web page at: http://%s", ip_address);
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
    const struct mmwlan_s1g_channel_list* channel_list;
    struct mmwlan_sta_args sta_args = MMWLAN_STA_ARGS_INIT;
    uint8_t mac_addr[MMWLAN_MAC_ADDR_LEN];
    bool ok;
    uint32_t countdown;

    printf("\n\nMorse Scan + Connect Demo (Built "__DATE__ " " __TIME__ ")\n\n");

    /* Create semaphore for link up signaling. */
    link_up_semaphore = mmosal_semb_create("link_up");
    MMOSAL_ASSERT(link_up_semaphore != NULL);

    /* Initialize Morse subsystems, note that they must be called in this order. */
    mmhal_init();
    mmwlan_init();

    channel_list = mmwlan_lookup_regulatory_domain(get_regulatory_db(), COUNTRY_CODE);
    if (channel_list == NULL)
    {
        printf("Could not find specified regulatory domain matching country code %s\n",
               COUNTRY_CODE);
        MMOSAL_ASSERT(false);
    }
    status = mmwlan_set_channel_list(channel_list);
    if (status != MMWLAN_SUCCESS)
    {
        printf("Failed to set country code %s\n", channel_list->country_code);
        MMOSAL_ASSERT(false);
    }

    /* Boot the WLAN interface so that we can retrieve the firmware version. */
    struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
    (void)mmwlan_boot(&boot_args);

    /* Disable power save mode to ensure maximum responsiveness */
    status = mmwlan_set_power_save_mode(MMWLAN_PS_DISABLED);
    if (status != MMWLAN_SUCCESS)
    {
        printf("Warning: Failed to disable power save mode (status=%d)\n", status);
    }
    else
    {
        printf("Power save mode disabled\n");
    }

    app_print_version_info();

    /* Initialize IP stack with DHCP */
    printf("\n===== Initializing IP Stack with DHCP =====\n");
    struct mmipal_init_args mmipal_init_args = MMIPAL_INIT_ARGS_DEFAULT;
    mmipal_init_args.mode = MMIPAL_DHCP;  /* Explicitly enable DHCP */
    printf("Initializing IPv4 using DHCP...\n");

    /* Register callback BEFORE mmipal_init to catch IP assignment */
    mmipal_set_link_status_callback(link_status_callback);

    if (mmipal_init(&mmipal_init_args) != MMIPAL_SUCCESS)
    {
        printf("ERROR: Failed to initialize network interface\n");
        MMOSAL_ASSERT(false);
    }
    printf("IP stack initialized successfully\n");

    /* =============== SCAN PHASE =============== */
    printf("\n===== Starting Scan Phase =====\n");
    num_scan_results = 0;
    struct mmwlan_scan_req scan_req = MMWLAN_SCAN_REQ_INIT;
    scan_req.scan_rx_cb = scan_rx_callback;
    scan_req.scan_complete_cb = scan_complete_callback;
    status = mmwlan_scan_request(&scan_req);
    if (status != MMWLAN_SUCCESS)
    {
        printf("ERROR: mmwlan_scan_request failed with status %d\n", status);
        printf("Skipping scan and proceeding directly to connect...\n");
        scan_completed = true;  /* Skip scan */
    }
    else
    {
        printf("Scan started on %s channels, Waiting for results...\n", channel_list->country_code);

        /* Wait for scan to complete with timeout. */
        uint32_t scan_timeout_ms = 60000;  /* 60 second timeout */
        uint32_t elapsed_ms = 0;
        while (!scan_completed && elapsed_ms < scan_timeout_ms)
        {
            mmosal_task_sleep(100);
            elapsed_ms += 100;

            /* Print progress every 5 seconds */
            if (elapsed_ms % 5000 == 0)
            {
                printf("  Scan in progress... %lu seconds elapsed, %d results so far\n",
                       (unsigned long)(elapsed_ms / 1000), num_scan_results);
            }
        }

        if (!scan_completed)
        {
            printf("\nWARNING: Scan timed out after %lu seconds!\n",
                   (unsigned long)(scan_timeout_ms / 1000));
            printf("This may indicate a hardware or firmware issue.\n");
            printf("Proceeding to connection attempt anyway...\n");
        }
        else if (num_scan_results == 0)
        {
            printf("\nNo networks found during scan.\n");
            printf("This is normal if no HaLow APs are in range.\n");
        }
    }

    /* =============== DELAY BEFORE CONNECT =============== */
    printf("\n===== Waiting %lu seconds before connecting to '%s' =====\n",
           (unsigned long)SCAN_TO_CONNECT_DELAY_S, SSID);
    for (countdown = SCAN_TO_CONNECT_DELAY_S; countdown > 0; countdown--)
    {
        printf("  Connecting in %lu seconds...\n", (unsigned long)countdown);
        mmosal_task_sleep(1000);
    }

    /* =============== CONNECT PHASE =============== */
    printf("\n===== Starting Connect Phase =====\n");

    /* Register callback to be invoked when the link goes up and down. */
    status = mmwlan_register_link_state_cb(link_state_change_handler, link_up_semaphore);
    if (status != MMWLAN_SUCCESS)
    {
        printf("Failed to register link state callback\n");
        MMOSAL_ASSERT(false);
    }

    /* Register a callback to be invoked on receive. */
    status = mmwlan_register_rx_cb(rx_handler, NULL);
    if (status != MMWLAN_SUCCESS)
    {
        printf("Failed to register rx callback\n");
        MMOSAL_ASSERT(false);
    }

    status = mmwlan_get_mac_addr(mac_addr);
    if (status != MMWLAN_SUCCESS)
    {
        printf("Failed to get MAC address\n");
        MMOSAL_ASSERT(false);
    }
    printf("MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);

    /* Set up STA arguments and start connection to AP. */
    sta_args.ssid_len = sizeof(SSID) - 1;
    memcpy(sta_args.ssid, SSID, sta_args.ssid_len);
    sta_args.passphrase_len = sizeof(PASSPHRASE) - 1;
    memcpy(sta_args.passphrase, PASSPHRASE, sta_args.passphrase_len);
    sta_args.security_type = MMWLAN_SAE;

    printf("Attempting to connect to SSID: %s\n", SSID);
    status = mmwlan_sta_enable(&sta_args, sta_status_handler);
    if (status != MMWLAN_SUCCESS)
    {
        printf("Failed to enable STA mode: status %d\n", status);
        MMOSAL_ASSERT(false);
    }

    /* Wait until the link comes up (with timeout). */
    printf("Waiting for link to come up...\n");
    ok = mmosal_semb_wait(link_up_semaphore, 30000);  /* 30 second timeout */
    if (!ok)
    {
        printf("Timeout waiting for link up. Connection failed.\n");
        printf("Check that:\n");
        printf("  - AP '%s' is in range and broadcasting\n", SSID);
        printf("  - Passphrase is correct\n");
        printf("  - AP is configured for 802.11ah (WiFi HaLow)\n");
        while (1)
        {
            mmosal_task_sleep(1000);
        }
    }

    printf("\n===== Successfully Connected! =====\n");

    /* Wait for IP address assignment via DHCP */
    printf("Waiting for DHCP to assign IP address...\n");
    uint32_t wait_count = 0;
    while (strcmp(ip_address, "0.0.0.0") == 0 && wait_count < 100)
    {
        mmosal_task_sleep(100);
        wait_count++;
    }

    if (strcmp(ip_address, "0.0.0.0") == 0)
    {
        printf("WARNING: No IP address assigned after 10 seconds\n");
        printf("Continuing anyway...\n");
    }

    /* Start HTTP web server */
    printf("\n===== Starting Web Server =====\n");
    if (start_webserver() == ESP_OK)
    {
        printf("\n*** Web server is running! ***\n");
        printf("*** Visit http://%s in your browser ***\n\n", ip_address);
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
