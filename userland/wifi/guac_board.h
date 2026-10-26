/* OnePlus 7 Pro Wi-Fi board profile, from Corey's merged stock GM1917 DTB. */
#ifndef GUAC_WIFI_BOARD_H
#define GUAC_WIFI_BOARD_H

/* Experimental bring-up enabled by Corey's request, 2026-10-26.
 * Regulator phandles and DMA ownership remain unverified. Never flash this
 * experimental image to boot_a/boot_b; use temporary fastboot boot only. */
#define GUAC_WIFI_HARDWARE_ENABLED 1

/* GM1917 /reserved-memory/modem_region, WLAN, and /soc/qcom,icnss. */
#define GUAC_WIFI_MODEM_PA   0x8dc00000ull
#define GUAC_WIFI_MODEM_SIZE 0x09600000ull
#define GUAC_WIFI_MSA_PA     0x8bc00000ull
/* icnss requests 1 MiB MSA inside a 1.5 MiB reserved region. */
#define GUAC_WIFI_MSA_SIZE          0x00100000u
#define GUAC_WIFI_MSA_RESERVED_SIZE 0x00180000u

#define GUAC_WIFI_WCN_PA    0x18800000ull
#define GUAC_WIFI_SMMU_PA   0x15000000ull
#define GUAC_WIFI_SMMU_SIZE 0x00100000u
#define GUAC_WIFI_SID       0x640u
#define GUAC_WIFI_SID_MASK  0x01u

/* Reuse Pixel's 16 MiB DMA pool for an experiment. It lies in GM1917's
 * reported low DDR and outside XNU's highmem window, but firmware ownership
 * at this address has NOT been verified. */
#define GUAC_WIFI_DMA_POOL_PA   0xe0000000ull
#define GUAC_WIFI_DMA_POOL_SIZE 0x01000000ull

#define GUAC_WIFI_BDF_PATH "/var/local/fw/modem/bdwlan-guacamole.bin"
#define GUAC_WIFI_BDF_PATH2 "/var/local/fw/modem/bdwlan-guacamole-PROTO1.1.bin"

#if GUAC_WIFI_MSA_SIZE > GUAC_WIFI_MSA_RESERVED_SIZE
#error "WLFW MSA request exceeds the OnePlus reserved WLAN firmware region"
#endif
#if GUAC_WIFI_HARDWARE_ENABLED && GUAC_WIFI_DMA_POOL_PA == 0
#error "Choose and verify an exclusively owned DMA pool before starting WLAN"
#endif

#endif
