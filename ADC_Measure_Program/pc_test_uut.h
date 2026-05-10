#ifndef PC_TEST_UUT_H
#define PC_TEST_UUT_H

#include <stdint.h>

#define UUT_IP	"192.168.10.2"
#define PC_IP  	"192.168.10.3"
#define UUT_PORT 5005
#define TIMEOUT_SEC 10
#define	TEST_SUCCESS 	0x01
#define	TEST_FAILURE 	0x00
#define MAX_PATTERN_LEN    255

#define ADC_TEST     16

/**
 * @brief Command structure sent to UUT.
 * Protocol: [Opcode(1B)] [Length(2B)] [Data(nB)] [CRC(4B)] 
 * Total Packet = 3 + Length + 4 
 */
typedef struct __attribute__((packed)) {
    uint8_t  opcode;
    uint16_t length;
    uint8_t  data[255]; 
} packet_t;

#endif /* PC_TEST_UUT_H */
