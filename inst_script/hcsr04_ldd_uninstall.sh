#!/bin/bash

module="hcsr04_driver.ko"
device="hcs04_driver"

rm -f /dev/${device}
rmmod ${module}



