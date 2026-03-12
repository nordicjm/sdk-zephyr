rm -rf * && cmake -GNinja -DBOARD=nrf52840dk/nrf52840 -DAPP_DIR=.. -DSB_CONFIG_SECURE_BOOT_APPCORE=y -DSB_CONFIG_PARTITION_MANAGER=y -DSB_CONFIG_DFU_ZIP=n /home/jamie/PM_MCUBOOT/zephyr/share/sysbuild
