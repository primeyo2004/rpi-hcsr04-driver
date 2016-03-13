#!/bin/bash

module="hcsr04_driver.ko"
device="hcsr04_driver"
insmod ${module}

rm -f /dev/${device}   

major=`cat /proc/devices | awk "{if(\\$2==\"$device\") print \\$1}"`

mknod /dev/${device} c $major 0

chmod 666 /dev/${device}

chown pi /dev/${device}




