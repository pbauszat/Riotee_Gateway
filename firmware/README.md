cd /opt/zephyr

source .venv/bin/activate.fish

west build -b nrf52840dk_nrf52840 -d build_mcuboot bootloader/mcuboot/boot/zephyr
west flash -d build_mcuboot

west build -b nrf52840dk_nrf52840 zephyr/samples/subsys/mgmt/mcumgr/smp_svr -- -DOVERLAY_CONFIG=overlay-cdc.conf -DDTC_OVERLAY_FILE=usb.overlay
west sign -t imgtool -- --key bootloader/mcuboot/root-rsa-2048.pem
west flash --bin-file build/zephyr/zephyr.signed.bin


nrfjprog --eraseall
nrfjprog -f nrf52 --program /opt/nRF5_SDK_17.0.2/components/softdevice/s140/hex/s140_nrf52_7.2.0_softdevice.hex --sectorerase

cd /opt/nRF5_SDK_17.0.2/examples/dfu/open_bootloader
add -DNRF_DFU_DEBUG_VERSION to makefile
make flash

Change memory layout:
  FLASH (rx) : ORIGIN = 0x1000, LENGTH = 0xff000
  RAM (rwx) :  ORIGIN = 0x20000008, LENGTH = 0x3fff8

nrfutil pkg generate --hw-version 52 --sd-req=0x00 --application build/zephyr/zephyr.hex --application-version 1 blinky.zip
nrfutil dfu usb-serial -pkg blinky.zip -p /dev/ttyACM1

nrf52840dk_nrf52840 produces image that starts at 0x0
nrf52840dongle_nrf52840 produces image that starts at 0x1000