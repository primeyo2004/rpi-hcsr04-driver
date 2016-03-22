# rpi-hcsr04-driver

**rpi-hcsr04-driver** is a HC-SR04 (Ultrasonic Ranging Sensor)  Linux Device Driver written for Raspberry PI 2  with the following capabilities/features:

- **Asynchronous design & Hardware interrupt-driven** -- handling of the **echo** response signal is more efficient as it's captured using hardware interrupt, that means the signal is detected by the system on the hardware level as opposed to the polling-based approach where the software has to loop and monitor the **echo** response signal state
 
- _[to be implemented]_ **Configurable GPIO pin assignments and timeout settings** -- allows users to choose their preferred GPIO pins and timeout settings to be used for the HC-SR04 device which can be done during the driver installation (e.g. insmod) along with its hardware connection

- _[to be implemented]_ **Supports non-blocking mode** -- allows the userspace application to use **select** and **poll** API which can be incorporated conveniently with other non-blocking IO devices

