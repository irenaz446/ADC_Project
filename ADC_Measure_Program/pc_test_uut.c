#include "pc_test_uut.h"
#include "test_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <zlib.h>

#define STATUS_SUCCESS  0
#define STATUS_COMM_FAILURE  -1
/**
 * @brief Logic to send a single UDP command and wait for a result.
 * @return 0 on success, -1 on communication failure.
 */
static int run_single_test(int sockfd, test_db_t *db, uint8_t periph_id, const char *name) {
    struct sockaddr_in addr = {
        .sin_family = AF_INET, 
        .sin_port = htons(UUT_PORT), 
        .sin_addr.s_addr = inet_addr(UUT_IP)
    };
    
    uint8_t tx_buffer[7]; // 1 (opcode) + 2 (length) + 4 (crc)
    uint8_t rx_buffer[1024];
    struct timespec start, end;
    socklen_t slen = sizeof(addr);

    // 1. Setup Command: No data payload, so length = 0
    tx_buffer[0] = periph_id; // Opcode 
    tx_buffer[1] = 0x00;      // Length Low Byte 
    tx_buffer[2] = 0x00;      // Length High Byte
    
    // 2. CRC calculation (calculated over the 3-byte header only)
    uint32_t tx_crc = crc32(0L, tx_buffer, 3);

    // 3. Append CRC to the buffer (bytes 3,4,5,6)
    tx_buffer[3] = (uint8_t)(tx_crc & 0xFF);
    tx_buffer[4] = (uint8_t)((tx_crc >> 8) & 0xFF);
    tx_buffer[5] = (uint8_t)((tx_crc >> 16) & 0xFF);
    tx_buffer[6] = (uint8_t)((tx_crc >> 24) & 0xFF);

    printf("Requesting %s measurement... ", name);
    fflush(stdout);

    clock_gettime(CLOCK_MONOTONIC, &start);

    // 4. Send
    if (sendto(sockfd, tx_buffer, 7, 0, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        return -1;
    }

    // 5. Receive Response
    ssize_t received = recvfrom(sockfd, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr*)&addr, &slen);
    if (received < 0) {
        perror("recvfrom failed");
        return -1;
    }
    else if (received < 7) { 
        printf("FAILED (Timeout) Result: %lu\n", received);
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double dur = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    // 6. Verify Response CRC
    uint32_t rx_payload_len = (uint32_t)received - 4;
    uint32_t expected_crc, actual_crc;
    memcpy(&expected_crc, &rx_buffer[rx_payload_len], 4);
    actual_crc = crc32(0L, (const unsigned char*)rx_buffer, rx_payload_len);

    if (actual_crc != expected_crc) {
        printf("FAILED (CRC Mismatch)\n");
        return -1;
    }

    // 7. Extract Results from Data (Data starts at index 3)
    uint16_t adc_result = 0;
    uint16_t data_len;
    float measured_val = 0.0f;
    memcpy(&data_len, &rx_buffer[1], 2);

    if (data_len >= 2) {
        memcpy(&adc_result, &rx_buffer[3], 2);
        // Convert 12-bit ADC to Voltage (3.3V ref)
        measured_val = (adc_result / 4095.0f) * 3.3f;
        printf("SUCCESS - Value: %u (%.3f V)\n", adc_result, measured_val);
    } else {
        printf("SUCCESS (No data returned)\n");
    }

    return test_db_save(db, (uint32_t)time(NULL), dur, name, "SUCCESS", measured_val);
}

int main(int argc, char *argv[]) {
    int sockfd = STATUS_COMM_FAILURE;
    test_db_t *db = NULL;
    int status = EXIT_FAILURE;

    db = test_db_init("test_results.db");
    if (db == NULL) {
        fprintf(stderr, "Database error\n");
        return EXIT_FAILURE;
    }

    /* Print on demand */
    if (argc == 2 && strcmp(argv[1], "--report") == 0) {
        test_db_print_report(db);
        status = EXIT_SUCCESS;
        goto cleanup;
    }

    if (argc == 2 && strcmp(argv[1], "--clean") == 0) {
        if (test_db_clear(db) == 0) {
            printf("Database cleaned successfully.\n");
            status = EXIT_SUCCESS;
        } else {
            fprintf(stderr, "Failed to clean database.\n");
            status = EXIT_FAILURE;
        }
        goto cleanup;
    }
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <peripheral_name>\n", argv[0]);
        goto cleanup;
    }

    /* Network Setup */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Socket error: %s\n", strerror(errno));
        goto cleanup;
    }

    // Resolve peripheral name to ID
    uint8_t id = 0;
    if (strcasecmp(argv[1], "adc") == 0) id = ADC_TEST;

    if (id == 0) {
        fprintf(stderr, "Unknown peripheral: %s\n", argv[1]);
        close(sockfd);
        return 1;
    }

    struct timeval tv = { .tv_sec = TIMEOUT_SEC };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (run_single_test(sockfd, db, id, argv[1]) == 0) {
        status = EXIT_SUCCESS;
    }

cleanup:
    if (sockfd != STATUS_COMM_FAILURE) { 
        close(sockfd); 
    }
    test_db_destroy(db);
    return status;
}