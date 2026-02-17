#include "dns_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "dns_server";

#define DNS_PORT       53
#define DNS_MAX_LEN    256
#define AP_IP_ADDR     0x0104A8C0  // 192.168.4.1 in network byte order

#define QR_FLAG        (1 << 7)
#define OPCODE_MASK    (0x7800)
#define QD_TYPE_A      0x0001
#define ANS_TTL_SEC    300

typedef struct __attribute__((__packed__)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

typedef struct __attribute__((__packed__)) {
    uint16_t ptr_offset;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t addr_len;
    uint32_t ip_addr;
} dns_answer_t;

static TaskHandle_t dns_task_handle = NULL;
static volatile bool running = false;

// Skip over a DNS name in the packet, return pointer past it
static char *skip_dns_name(char *ptr)
{
    while (*ptr != 0) {
        ptr += (*ptr) + 1;
    }
    return ptr + 1;  // skip null terminator
}

// Build DNS response: answer all A-type queries with the AP IP
static int build_dns_response(char *req, int req_len, char *reply, int reply_max)
{
    if (req_len > reply_max) return -1;

    memset(reply, 0, reply_max);
    memcpy(reply, req, req_len);

    dns_header_t *header = (dns_header_t *)reply;

    // Only handle standard queries
    if ((header->flags & OPCODE_MASK) != 0) return 0;

    header->flags |= QR_FLAG;

    uint16_t qd_count = ntohs(header->qd_count);
    header->an_count = htons(qd_count);

    int reply_len = req_len + qd_count * sizeof(dns_answer_t);
    if (reply_len > reply_max) return -1;

    char *qd_ptr = reply + sizeof(dns_header_t);
    char *ans_ptr = reply + req_len;

    for (int i = 0; i < qd_count; i++) {
        char *name_start = qd_ptr;
        qd_ptr = skip_dns_name(qd_ptr);

        uint16_t qtype = ntohs(*(uint16_t *)qd_ptr);
        uint16_t qclass = ntohs(*(uint16_t *)(qd_ptr + 2));
        qd_ptr += 4;  // skip type + class

        if (qtype == QD_TYPE_A) {
            dns_answer_t *answer = (dns_answer_t *)ans_ptr;
            answer->ptr_offset = htons(0xC000 | (name_start - reply));
            answer->type = htons(qtype);
            answer->class = htons(qclass);
            answer->ttl = htonl(ANS_TTL_SEC);
            answer->addr_len = htons(4);
            answer->ip_addr = AP_IP_ADDR;
            ans_ptr += sizeof(dns_answer_t);
        }
    }

    return reply_len;
}

static void dns_server_task(void *pvParameters)
{
    char rx_buf[512];
    char reply[DNS_MAX_LEN];

    while (running) {
        struct sockaddr_in dest = {
            .sin_addr.s_addr = htonl(INADDR_ANY),
            .sin_family = AF_INET,
            .sin_port = htons(DNS_PORT),
        };

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Socket create failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (bind(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
            ESP_LOGE(TAG, "Socket bind failed: errno %d", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "DNS server listening on port %d", DNS_PORT);

        while (running) {
            struct sockaddr_in source;
            socklen_t socklen = sizeof(source);
            int len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                               (struct sockaddr *)&source, &socklen);
            if (len < 0) {
                if (running) {
                    ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                }
                break;
            }

            int reply_len = build_dns_response(rx_buf, len, reply, sizeof(reply));
            if (reply_len > 0) {
                sendto(sock, reply, reply_len, 0,
                       (struct sockaddr *)&source, sizeof(source));
            }
        }

        close(sock);
    }

    ESP_LOGI(TAG, "DNS server stopped");
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void)
{
    if (running) return ESP_OK;

    running = true;
    BaseType_t ret = xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle);
    if (ret != pdPASS) {
        running = false;
        ESP_LOGE(TAG, "Failed to create DNS server task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void dns_server_stop(void)
{
    if (!running) return;

    running = false;
    // The task will exit on its own after recvfrom times out or errors
    if (dns_task_handle) {
        // Give task time to exit cleanly
        vTaskDelay(pdMS_TO_TICKS(100));
        dns_task_handle = NULL;
    }
    ESP_LOGI(TAG, "DNS server stop requested");
}

bool dns_server_is_running(void)
{
    return running;
}
