/*
 * DNS Server for Captive Portal
 * Redirects all queries to the SoftAP IP address
 */
#include <string.h>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "dns_server";

void dns_server_task(void *pvParameters)
{
    char rx_buffer[512];
    char tx_buffer[512];
    int sock = -1;
    struct sockaddr_in dest_addr;

    // Set up UDP socket
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(53);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS Server started on port 53");

    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    while (1) {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        }

        // DNS Header Processing (Simplified)
        // We just want to spoof the answer to point to our IP
        if (len > 12) {
            // Prepare response
            memcpy(tx_buffer, rx_buffer, len); // Copy query to response

            // Set QR to response (bit 15), AA (Authoritative Answer, bit 10)
            tx_buffer[2] |= 0x84;
            
            // Set RCODE to 0 (No Error) if it was a standard query
            if ((tx_buffer[2] & 0x78) == 0) {
                 tx_buffer[3] &= 0xF0; // Clear RCODE
            }
            
            // Set Answer Count to 1
            tx_buffer[6] = 0x00;
            tx_buffer[7] = 0x01;

            // Pointer to the queried name in the question section
            // In a real DNS server we would parse the question section to find where it ends.
            // For this simple captive portal, we assume the question section ends at 'len'
            // and we append the answer resource record.
            
            // However, constructing a valid DNS packet requires parsing the QNAME.
            // Simplified approach: Find the end of QNAME.
            int qname_len = 0;
            char *p = rx_buffer + 12;
            while(p < rx_buffer + len && *p != 0) {
                qname_len++;
                p++;
            }
            if (p >= rx_buffer + len) {
                 continue; // Malformed packet
            }
            qname_len++; // Include null terminator

            int qtype_offset = 12 + qname_len;
            
            // We only answer A records (Type 1)
            if (qtype_offset + 4 <= len) {
                // Check QTYPE (bytes at qtype_offset and qtype_offset+1)
                // 0x00 0x01 is A record
                if (rx_buffer[qtype_offset] == 0x00 && rx_buffer[qtype_offset+1] == 0x01) {
                    // Construct Answer
                    // Name ptr (0xC00C points to header + 12)
                    int ans_offset = len;
                    
                    // Check buffer space
                    if (ans_offset + 16 < sizeof(tx_buffer)) {
                        tx_buffer[ans_offset++] = 0xC0;
                        tx_buffer[ans_offset++] = 0x0C;
                        
                        // Type: A (0x0001)
                        tx_buffer[ans_offset++] = 0x00;
                        tx_buffer[ans_offset++] = 0x01;
                        
                        // Class: IN (0x0001)
                        tx_buffer[ans_offset++] = 0x00;
                        tx_buffer[ans_offset++] = 0x01;
                        
                        // TTL: 60s
                        tx_buffer[ans_offset++] = 0x00;
                        tx_buffer[ans_offset++] = 0x00;
                        tx_buffer[ans_offset++] = 0x00;
                        tx_buffer[ans_offset++] = 0x3C;
                        
                        // Data Length: 4
                        tx_buffer[ans_offset++] = 0x00;
                        tx_buffer[ans_offset++] = 0x04;
                        
                        // IP Address: 192.168.4.1 (C0 A8 04 01)
                        tx_buffer[ans_offset++] = 192;
                        tx_buffer[ans_offset++] = 168;
                        tx_buffer[ans_offset++] = 4;
                        tx_buffer[ans_offset++] = 1;

                        sendto(sock, tx_buffer, ans_offset, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                    }
                }
            }
        }
    }

    close(sock);
    vTaskDelete(NULL);
}

static TaskHandle_t s_dns_server_task_handle = NULL;

void start_dns_server(void)
{
    if (s_dns_server_task_handle) {
        ESP_LOGW(TAG, "DNS server already running");
        return;
    }
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &s_dns_server_task_handle);
}

void stop_dns_server(void)
{
    if (s_dns_server_task_handle) {
        vTaskDelete(s_dns_server_task_handle);
        s_dns_server_task_handle = NULL;
    }
}
