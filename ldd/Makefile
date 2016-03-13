#author: Jeune Prime Origines
#decription: Makefile for HCSR04 Ultrasonic Ranging Sensor driver (Linux)

obj-m += hcsr04_driver.o
hcsr04_driver-objs += hcsr04_async_device.o hcsr04_cdrv.o

KDIR=${KERNEL_SRC} 

all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	rm -rf *.o *.ko *.mod  *.symvers *.order .*.cmd
