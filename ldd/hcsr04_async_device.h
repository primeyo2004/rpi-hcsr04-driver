/*
 * A Linux device driver for HC-SR04 Ultrasonic sensor interfaced with Raspberry PI 2 GPIO 
 * Copyright (C) 2016  Jeune Prime M. Origines <primeyo2004@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * */


#ifndef __HCSR04_ASYNC_DEVICE_H
#define __HCSR04_ASYNC_DEVICE_H

#include <linux/err.h>


typedef enum {
   RRESULT_SUCCESS  = 0,
   RRESULT_IN_PROGRESS,
   RRESULT_TIMEDOUT,
   RRESULT_NOT_STARTED,
   RRESULT_UNKNOWN
} ranging_result_t;

#define SUCCESS 0

/* asynchronous interface function */

extern int init_ranging_device(
      unsigned int trigger_gpio,
      unsigned int echo_gpio,
      unsigned int usec_pulse_width,
      unsigned int usec_timeout,
      bool blocking,
      void**   pprivata_data);

extern int release_ranging_device(void* private_data);

extern int start_async_ranging(void* private_data);

extern int reset_async_ranging(void* private_data);

extern int read_async_ranging_result(
      void* private_data,
      ranging_result_t* result_code,
      struct timespec* start_time,
      struct timespec* end_time,
      struct timespec* delta_time);


#endif
