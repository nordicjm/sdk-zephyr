nrfjprog -f NRF53 --coprocessor CP_NETWORK --recover
nrfjprog -f NRF53 --coprocessor CP_NETWORK --program ./hci_rpmsg/zephyr/merged_CPUNET.hex --sectorerase
nrfjprog -f NRF53 --program ./mcuboot/zephyr/zephyr.hex --sectorerase
nrfjprog -f NRF53 --program ./zephyr/internal_flash_signed.hex --sectorerase
nrfjprog -f NRF53 --program ./zephyr/qspi_flash_signed.hex --qspisectorerase
nrfjprog -f NRF53 --reset
