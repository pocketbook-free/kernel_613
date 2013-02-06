#!/bin/sh

[ "$HOSTNAME" = "luna-10" -o "$HOSTNAME" = "chaos" ] && exec ./make-dz.sh "$@"

PROJECT=fc613

export PATH="/opt/freescale/usr/local/gcc-4.4.4-glibc-2.11.1-multilib-1.0/arm-fsl-linux-gnueabi/bin/:$PATH"
CC=arm-fsl-linux-gnueabi-
#export PATH="/home/mike/x-tools/arm-pb_imx508-linux-gnueabi/bin:$PATH"
#CC=arm-pb_imx508-linux-gnueabi-
BUILD_DIR=/tmp/${PROJECT}_bin
HEADERS_DIR=/tmp/${PROJECT}_headers
if [ ! -d $BUILD_DIR ]
then
	mkdir $BUILD_DIR
fi

if [ ! -d $BUILD_DIR ]
then
	mkdir $HEADERS_DIR
fi

MOD_INSTALL_PATH=/tmp/${PROJECT}_modules
UIMAGE_PATH=${BUILD_DIR}/arch/arm/boot/uImage

if [ "$1" = "install" ]
then
	sudo dd if=$UIMAGE_PATH of=/dev/sdf bs=512 seek=2048 && sync && sync
	exit 0
fi

# to install created modules to rootfs repo, we should launch make.sh with
# root_install argument and give path to root of rootfs repo
# ./make.sh root_install ~/work/rootfs_611/
if [ "$1" = "root_install" ]
then
	set -x

	if [ -z "$2" ]
	then
		echo "No path to rootfs"
		exit 1
	fi

	pushd ${MOD_INSTALL_PATH}
	find . -type f -name "*.ko" -exec cp {} . \;
	cp arcotg_udc.ko $2/rootfs/lib/modules/
	cp g_zero.ko $2/rootfs/lib/modules/
	cp g_file_storage.ko $2/rootfs/lib/modules/
	popd

	exit 0
fi

do_fail()
{
	echo $1
	exit 1
}

#set -x

STABLE_KERNEL_CONFIG=nx611_20111010_defconfig
COMPILE_STAGES="mrproper ${STABLE_KERNEL_CONFIG} uImage modules modules_install"
MODULE_LIST="arcotg_udc\|g_file_storage\|g_zero"

TMP_DIR=/home/${USER}/tmp
ROOTFS_DIR=rootfs-${PROJECT}
ROOTFS_PATH=${TMP_DIR}/${ROOTFS_DIR}
ROOTFS_REPO='ssh://m.boiko@192.168.2.254:2222/repos/rootfs/nx611'

CURRENT_REVISION=`hg id -i`
CURRENT_COMMENT=`hg parent | grep ^summary | cut -d' ' -f 6-`

#BRANCH_NAME="hangfix_by_DZ"

# release kernel image and modules
if [ "$1" = "release_all" ]
then
	rm -rf /tmp/${PROJECT}_*

	make mrproper
	for i in ${COMPILE_STAGES}
	do
		$0 $i || do_fail "Failed stage: $i"
	done

	cd ${TMP_DIR} || do_fail "Cant open dir $TMP_DIR"
	rm -rf ${ROOTFS_PATH}
	hg clone ${ROOTFS_REPO} ${ROOTFS_DIR} || do_fail "Cant clone: ${ROOTFS_REPO}"
	cd ${ROOTFS_PATH} || do_fail "Cant open dir $ROOTFS_PATH"
	hg update ${BRANCH_NAME:-"default"} -C
	cp -v ${UIMAGE_PATH} uImage/uImage || do_fail "No kernel image: ${UIMAGE_PATH}"
	cd ${MOD_INSTALL_PATH} || do_fail "No such path: ${MOD_INSTALL_PATH}"
	cp -v $(find . -type f -name "*.ko" -print | grep ${MODULE_LIST}) ${ROOTFS_PATH}/rootfs/lib/modules
	cd -
	cd ${ROOTFS_PATH}
	hg commit -m "BSP version: ${CURRENT_REVISION}, summary:${CURRENT_COMMENT}"
	echo hg push
	exit 0
fi

which ccache &>/dev/null
if [ $? -eq 0 ]
then
	CPREFIX="ccache $CC"
else
	CPREFIX="$CC"
fi

#echo -n '-' > localversion
#hg id -i >> localversion

make INSTALL_HDR_PATH=$HEADERS_DIR INSTALL_MOD_PATH=$MOD_INSTALL_PATH CROSS_COMPILE="$CPREFIX" ARCH=arm O=$BUILD_DIR -j 2 $1
if [ $? -eq 0 ]
then
	RETVAL=0
else
	RETVAL=1
fi

cp -v ${BUILD_DIR}/.config arch/arm/configs/last_defconfig

exit $RETVAL

