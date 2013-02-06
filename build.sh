#!/bin/sh

make imx5_defconfig
make
make uImage BSP_VERSION=1.0.3_20120613

cd ../bw-ub/
make ep8_config
make;sync

cd ../bw-kn/
cp ../bw-ub/u-boot.bin arch/arm/boot/;sync
cp drivers/usb/gadget/arcotg_udc.ko arch/arm/boot/;sync
cp drivers/usb/gadget/g_ether.ko arch/arm/boot/;sync
cp drivers/usb/gadget/g_file_storage.ko arch/arm/boot/;sync

ls -l arch/arm/boot/

echo ok
