#!/bin/bash
# Author: Jeune Prime M. Origines
# Decription: Just a build helper script for the arm-based (raspberry pi 2) linux device driver

#note: ensure that the KERNEL_SRC and CCPREFIX environment variable are set (e.g. in .bashrc)
#export KERNEL_SRC=~/codes/raspberrypi/kernel
#export CCPREFIX=~/codes/raspberrypi/tools/arm-bcm2708/arm-bcm2708-linux-gnueabi/bin/arm-bcm2708-linux-gnueabi-

make clean
make ARCH=arm CROSS_COMPILE=${CCPREFIX}

