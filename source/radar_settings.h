#ifndef XENSIV_BGT60TRXX_CONF_H
#define XENSIV_BGT60TRXX_CONF_H

#define XENSIV_BGT60TRXX_CONF_DEVICE (XENSIV_DEVICE_BGT60TR13C)
#define XENSIV_BGT60TRXX_CONF_START_FREQ_HZ (58500000000)
#define XENSIV_BGT60TRXX_CONF_END_FREQ_HZ (62500000000)
#define XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP (64)
#define XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME (32)
#define XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS (3)
#define XENSIV_BGT60TRXX_CONF_NUM_TX_ANTENNAS (1)
#define XENSIV_BGT60TRXX_CONF_SAMPLE_RATE (2000000)
#define XENSIV_BGT60TRXX_CONF_CHIRP_REPETITION_TIME_S (0.000299787)
#define XENSIV_BGT60TRXX_CONF_FRAME_REPETITION_TIME_S (0.0300446)  

#define XENSIV_BGT60TRXX_CONF_NUM_REGS (38)

#if defined(XENSIV_BGT60TRXX_CONF_IMPL)
const uint32_t register_list[] = {
            0x11e8270UL,
            0x30a0210UL,
            0x9e967fdUL,
            0xb0805b4UL,
            0xd102bffUL,
            0xf010d00UL,
            0x11000000UL,
            0x13000000UL,
            0x15000000UL,
            0x17000be0UL,
            0x19000000UL,
            0x1b000000UL,
            0x1d000000UL,
            0x1f000b60UL,
            0x2113fc51UL,
            0x237ff41fUL,
            0x25000c63UL,
            0x2d000490UL,
            0x3b000480UL,
            0x49000480UL,
            0x57000480UL,
            0x5911be0eUL,
            0x5b56040aUL,
            0x5d01f000UL,
            0x5f787e1eUL,
            0x61b12902UL,
            0x6300091dUL,
            0x65000172UL,
            0x67000040UL,
            0x69000000UL,
            0x6b000000UL,
            0x6d000000UL,
            0x6f2a0b10UL,
            0x7f000100UL,
            0x8f000100UL,
            0x9f000100UL,
            0xad000000UL,
            0xb7000000UL
};
#endif /* XENSIV_BGT60TRXX_CONF_IMPL */
#endif /* XENSIV_BGT60TRXX_CONF_H */
