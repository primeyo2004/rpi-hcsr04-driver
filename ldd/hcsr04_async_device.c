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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include "hcsr04_async_device.h"

#define INVALID_GPIO_NUM 0xFFFFFFFF
#define INVALID_IRQ_NUM  -1

/* controller status enumeration */
typedef enum {
  CONTROLLER_NONE  = 0, 
  CONTROLLER_REQUESTED,  /* when the userspace application calls write()*/
  CONTROLLER_TRIGGER_HI, /* we are about to send trigger hi signal to gpio */
  CONTROLLER_TRIGGER_LO, /* we are about to send trigger lo signal to gpio */
  CONTROLLER_TRIGGERED,  /* 10 microseconds triggered was succesfully sent */
  CONTROLLER_COMPLETED,  /* the echo IRQ has responsded before the timeout */
  CONTROLLER_TIMEDOUT,   /* the timeout ~100ms has elapse and the IRQ still not received */
  CONTROLLER_INVALID     /* invalid state */
} controller_status_t;


/* Event source flags */
typedef enum {
  EVENT_SRC_USR_WR       = 0x01,
  EVENT_SRC_USR_RD       = 0x02,
  EVENT_SRC_TRG_HI       = 0x04,
  EVENT_SRC_TRG_LO       = 0x08,
  EVENT_SRC_TIMEOUT      = 0x10,
  EVENT_SRC_INTERRUPT_RISE    = 0x20,
  EVENT_SRC_INTERRUPT_FALL    = 0x40
} event_src_flags_t;


struct range_data {
    struct timespec  start_time;                                                                             
    struct timespec  end_time;                                                                                
    struct timespec  delta_time;
};

struct gpio_config{
  unsigned int trigger_gpio;
  unsigned int echo_gpio; 
  int irq_num;
  unsigned int usec_pulse_width;
  unsigned int usec_timeout;
};


struct device_data {
   bool                  blocking;
   spinlock_t            lock;
   struct semaphore      ready_sem;


   controller_status_t   ctl_stat;                                                                                   
   u8                    evt_src_flags;

   struct range_data     range;
   struct gpio_config    gpio; 

   struct tasklet_struct controller_tasklet;
   struct timer_list     operation_timer;
};

static void async_controller_tasklet_func(unsigned long arg);
static void async_operation_timer_func(unsigned long arg);
static irqreturn_t irq_handler(int irq,void* dev_id);

char   DEVICE_NAME[] = "hcsr04_driver";




/* implementation */
/* Initialize the ranging device */
int init_ranging_device(
      unsigned int trigger_gpio,
      unsigned int echo_gpio,
      unsigned int usec_pulse_width,
      unsigned int usec_timeout,
      bool blocking,
      void** pprivate_data){
   int retval = SUCCESS;
   int temp_irq_num;

   struct device_data* pdev_data = (struct device_data*)(*pprivate_data);


   if (pdev_data != NULL){
      printk (KERN_ALERT "%s: Device may already have been initialized!\n",DEVICE_NAME);
      retval = -EBADFD;
      goto exit_func;
   }


   if ((pdev_data = kmalloc(sizeof(struct device_data),GFP_ATOMIC)) == NULL){
      printk (KERN_ALERT "%s: Unable to allocate memory.\n", DEVICE_NAME);
      retval = -ENOMEM;
      goto exit_func;
   }

   memset(pdev_data,0x00,sizeof(struct device_data));

   pdev_data->blocking = blocking;

   spin_lock_init(&pdev_data->lock);
   sema_init (&pdev_data->ready_sem,1);

   pdev_data->ctl_stat = CONTROLLER_NONE;
   pdev_data->evt_src_flags = 0;

   
   pdev_data->gpio.trigger_gpio = INVALID_GPIO_NUM;
   pdev_data->gpio.echo_gpio    = INVALID_GPIO_NUM;
   pdev_data->gpio.irq_num      = INVALID_IRQ_NUM;
   pdev_data->gpio.usec_pulse_width = usec_pulse_width;
   pdev_data->gpio.usec_timeout     = usec_timeout;


   memset(&pdev_data->range,0x00,sizeof(pdev_data->range));

   if ((retval = gpio_request_one(
         trigger_gpio,
         GPIOF_DIR_OUT |
         GPIOF_OUT_INIT_LOW|
         GPIOF_OPEN_SOURCE,
         "hcsr04 trigger gpio")) != SUCCESS){

      printk (KERN_ALERT "%s: Failed to request trigger gpio %d.\n",DEVICE_NAME,trigger_gpio);

      goto exit_func;
   }

   pdev_data->gpio.trigger_gpio = trigger_gpio;


   if ((retval = gpio_request_one(
         echo_gpio,
         GPIOF_IN, 
         "hcsr04 echo gpio")) != SUCCESS){

      printk (KERN_ALERT "%s: Failed to request echo  gpio %d.\n",DEVICE_NAME,echo_gpio);

      goto exit_func;
   }

   pdev_data->gpio.echo_gpio = echo_gpio;

   temp_irq_num  = gpio_to_irq(echo_gpio);

   if ((retval = request_irq (
               temp_irq_num,
               irq_handler,
               IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING ,
               "hcsr04 gpio echo interrupt-handler",
               pdev_data
               )) != SUCCESS){

      printk (KERN_ALERT "%s: Failed to request irq handler for irq num  %d,  gpio %d.\n",
            DEVICE_NAME,
            temp_irq_num,
            echo_gpio);

      goto exit_func;
   }
   pdev_data->gpio.irq_num = temp_irq_num;


   tasklet_init (
         &pdev_data->controller_tasklet,
         async_controller_tasklet_func,
         (unsigned long)pdev_data);

   setup_timer (
         &pdev_data->operation_timer,
         async_operation_timer_func,
         (unsigned long)pdev_data);


   pdev_data->gpio.irq_num = temp_irq_num;

   *pprivate_data = pdev_data;


exit_func:
   if (retval != SUCCESS ){
      release_ranging_device(*pprivate_data);
      *pprivate_data = NULL;
   }

   return retval;
}

/* releases (uninitialize) the ranging device */
int release_ranging_device(void* private_data){

   struct device_data* pdev_data = (struct device_data*)private_data;
   unsigned long flags;

   if (pdev_data == NULL){
      printk (KERN_ALERT "%s: Device might have not yet been initialized! \n",DEVICE_NAME);
      goto exit_func;
   }

   spin_lock(&pdev_data->lock);
   local_irq_save(flags);

   /* uninstall the interrupts, kill any timers and tasklets */
   if (pdev_data->gpio.irq_num != INVALID_IRQ_NUM){
      free_irq(pdev_data->gpio.irq_num,pdev_data);
      pdev_data->gpio.irq_num = INVALID_IRQ_NUM;
   }

   del_timer (&pdev_data->operation_timer);
   tasklet_kill (&pdev_data->controller_tasklet);

   local_irq_restore(flags);
   spin_unlock(&pdev_data->lock);



   if ( pdev_data->gpio.echo_gpio != INVALID_GPIO_NUM ){
      gpio_free (pdev_data->gpio.echo_gpio);
      pdev_data->gpio.echo_gpio = INVALID_GPIO_NUM;
   }

   if ( pdev_data->gpio.trigger_gpio != INVALID_GPIO_NUM ){
      gpio_free (pdev_data->gpio.trigger_gpio);
      pdev_data->gpio.trigger_gpio = INVALID_GPIO_NUM;
   }


   /* make sure that the semaphore is back to 1 */
   if (down_trylock (&pdev_data->ready_sem)){
      up(&pdev_data->ready_sem);
   }


   kfree (pdev_data);
   pdev_data = NULL;

   printk (KERN_INFO "%s: Device released.\n",DEVICE_NAME);

exit_func:

   return SUCCESS;
}

int start_async_ranging(void* private_data){
   int retval = SUCCESS;
   unsigned long flags;
   struct device_data* pdev_data = (struct device_data*)private_data;

   ranging_result_t result_code;
   
   if ((retval = read_async_ranging_result (
               private_data,
               &result_code,
               NULL,
               NULL,
               NULL)) != SUCCESS){
      printk (KERN_ALERT "%s: Failed to read async ranging result\n",DEVICE_NAME);
      goto exit_func;
   }


   switch (result_code){
      case RRESULT_NOT_STARTED:
         // All is well so far
         break;
      case RRESULT_IN_PROGRESS:
         retval = -EAGAIN;
         goto exit_func;
         break;
      case RRESULT_SUCCESS:
      case RRESULT_TIMEDOUT:
      case RRESULT_UNKNOWN:
      default:
         /* invalid file descriptor state */
         /* client must call reset_async_ranging() function to reset
          * success,timed_out,unknown state */
         retval = -EBADFD;
         goto exit_func;
         break;
   }

   local_irq_save(flags);
   spin_lock (&pdev_data->lock);

   pdev_data->ctl_stat = CONTROLLER_REQUESTED;
   tasklet_schedule (&pdev_data->controller_tasklet);

   spin_unlock (&pdev_data->lock);
   local_irq_restore (flags);

   /* ensure that we have acquire the semaphore once 
    * the async ranging has already started*/
   if (down_trylock(&pdev_data->ready_sem)){
      retval = -EBADFD;
      printk (KERN_ALERT "%s: Failed acquire the semaphore lock\n",DEVICE_NAME);
      goto exit_func;
   }
   

exit_func:
   return retval;
}

int reset_async_ranging(void* private_data){
   int retval = SUCCESS;
   ranging_result_t result_code;
   unsigned long flags;
   struct device_data* pdev_data = (struct device_data*)private_data;

   
   if ((retval = read_async_ranging_result (
               private_data,
               &result_code,
               NULL,
               NULL,
               NULL)) != SUCCESS){
      printk (KERN_ALERT "%s: Failed to read async ranging result\n",DEVICE_NAME);
      goto exit_func;
   }


   switch (result_code){
      case RRESULT_SUCCESS:
      case RRESULT_TIMEDOUT:
      case RRESULT_UNKNOWN:
      case RRESULT_NOT_STARTED:
        break;
      case RRESULT_IN_PROGRESS:
         retval = -EAGAIN;
         goto exit_func;
         break;
      default:
         /* invalid file descriptor state */
         /* client must call reset_async_ranging() function to reset
         * success,timed_out,unknown state */
         retval = -EBADFD;
         goto exit_func;
         break;
   }

   local_irq_save(flags);
   spin_lock (&pdev_data->lock);

   pdev_data->ctl_stat = CONTROLLER_NONE;

   spin_unlock (&pdev_data->lock);
   local_irq_restore (flags);

exit_func:
   return retval;
}

int read_async_ranging_result(
      void* private_data,
      ranging_result_t* result_code,
      struct timespec* start_time,
      struct timespec* end_time,
      struct timespec* delta_time){

   int retval = SUCCESS; 
   unsigned long flags;
   struct device_data* pdev_data = (struct device_data*)private_data;

   /* initialize the output parameters */
   *result_code = RRESULT_UNKNOWN;
   if (start_time){
      memset(start_time,0x00,sizeof(*start_time));
   }

   if (end_time){
      memset(end_time,0x00,sizeof(*end_time));
   }

   if (delta_time){
      memset(delta_time,0x00,sizeof(*delta_time));
   }

   if (!pdev_data){
      retval = -ENOMEM;
      printk (KERN_ALERT "%s: Invalid device data!\n",DEVICE_NAME);
      goto exit_func;
   }

   if ( pdev_data->blocking ){
      if ((retval = down_interruptible(&pdev_data->ready_sem)) != SUCCESS){

         *result_code = RRESULT_IN_PROGRESS;
         printk (KERN_ALERT "%s: Blocking wait for semaphore lock failed!\n",DEVICE_NAME);
         goto exit_func;
      }
   }
   else{

      if ( down_trylock(&pdev_data->ready_sem )){
         retval = -EAGAIN;
         *result_code = RRESULT_IN_PROGRESS;
         goto exit_func;
      }
   }


   up(&pdev_data->ready_sem);

   local_irq_save(flags);
   spin_lock(&pdev_data->lock);

   switch(pdev_data->ctl_stat){
      case CONTROLLER_NONE: 
         *result_code = RRESULT_NOT_STARTED;
         break;
      case CONTROLLER_REQUESTED:
      case CONTROLLER_TRIGGER_HI:
      case CONTROLLER_TRIGGER_LO:
      case CONTROLLER_TRIGGERED:
         *result_code = RRESULT_IN_PROGRESS;
         break;
      case CONTROLLER_COMPLETED:
         *result_code = RRESULT_SUCCESS;
         if (start_time){
            *start_time = pdev_data->range.start_time;
         }

         if (end_time){
            *end_time = pdev_data->range.end_time;
         }

         if (delta_time){
            *delta_time = pdev_data->range.delta_time;
         }

        break;
      case CONTROLLER_TIMEDOUT:
        *result_code = RRESULT_TIMEDOUT;
        break;
      case CONTROLLER_INVALID:
      default:
         *result_code = RRESULT_UNKNOWN;
         break;
   }


   spin_unlock(&pdev_data->lock);
   local_irq_restore(flags);

exit_func:
      
   return retval;
}



/* The asynchronous controller function */
static void async_controller_tasklet_func(unsigned long arg){

   struct device_data* pdev_data = (struct device_data*)arg;
   unsigned long flags;

   local_irq_save(flags);

   spin_lock(&pdev_data->lock);


   switch (pdev_data->ctl_stat){
    case CONTROLLER_REQUESTED:

       /* init the event source and set the controller stat to the
        * next state */
      pdev_data->evt_src_flags = 0;

      memset(&pdev_data->range,0x00,sizeof(pdev_data->range));


      pdev_data->ctl_stat = CONTROLLER_TRIGGER_HI;
      /* dispatch to the async timer the soonest for excution 
       * we need to send a trigger_gpio hi */
      mod_timer(&pdev_data->operation_timer,jiffies );

     break;

    case CONTROLLER_TRIGGER_HI:

      if (pdev_data->evt_src_flags & EVENT_SRC_TRG_HI){

         /* we need to send trigger_gpio lo 10us after the trigger_gpio hi
          * thus a 10us pulse, pdev_data->gpio.usec_pulse_width is typically 10us configurable */
         pdev_data->ctl_stat = CONTROLLER_TRIGGER_LO;
         mod_timer(&pdev_data->operation_timer,jiffies + usecs_to_jiffies (pdev_data->gpio.usec_pulse_width));
      }
      else{
         /* unexpected state */
         pdev_data->ctl_stat = CONTROLLER_INVALID;
         mod_timer(&pdev_data->operation_timer,jiffies);
      }

      break;
    case CONTROLLER_TRIGGER_LO:

      if (pdev_data->evt_src_flags & EVENT_SRC_TRG_LO){

         /* we just need a timeout watcher once we have delivered the 10us pulse
          * this is a fail safe code incase the ultrasonic sensor is unable to detect
          * the reflected waves (echo_gpio)
          */
         pdev_data->ctl_stat = CONTROLLER_TRIGGERED;
         mod_timer(&pdev_data->operation_timer,jiffies + usecs_to_jiffies (pdev_data->gpio.usec_timeout));
      }
      else{
         /* invalid state again */
         pdev_data->ctl_stat = CONTROLLER_INVALID;
         mod_timer(&pdev_data->operation_timer,jiffies);
     }

      break;
    case CONTROLLER_TRIGGERED:

      if (pdev_data->evt_src_flags & EVENT_SRC_TIMEOUT){
         /* The timeout watcher has kicked off */
         pdev_data->ctl_stat = CONTROLLER_TIMEDOUT;
         mod_timer(&pdev_data->operation_timer,jiffies);
      }
      else if (pdev_data->evt_src_flags & EVENT_SRC_INTERRUPT_RISE ){
         /* deactivate the async timer (e.g. timeout watcher)*/
         del_timer ( &pdev_data->operation_timer );

         if (pdev_data->evt_src_flags & EVENT_SRC_INTERRUPT_FALL ){
            
            /* our system has received the echo_gpio thru hardware interrupt */
            pdev_data->ctl_stat = CONTROLLER_COMPLETED;
            /* end_time will be populated by the IRQ handler to have better precision
             * hence we can only calculate the delta in here
             */
            pdev_data->range.delta_time = 
               timespec_sub(pdev_data->range.end_time,pdev_data->range.start_time);

            /* trigger the timer to finalize the result */
            mod_timer(&pdev_data->operation_timer,jiffies);
         }

      }
      else{
         /* invalid state */
         pdev_data->ctl_stat = CONTROLLER_INVALID;
         mod_timer(&pdev_data->operation_timer,jiffies);
      }

      break;

    default:
      /* invalid state */
      pdev_data->ctl_stat = CONTROLLER_INVALID;
      mod_timer(&pdev_data->operation_timer,jiffies);

      break;
   }

   spin_unlock(&pdev_data->lock);
   local_irq_restore(flags);

}

/* handles time triggered operations.
 * This function may handle calls that my sleep */  
static void async_operation_timer_func(unsigned long arg){

   struct device_data* pdev_data = (struct device_data*) arg;
   controller_status_t ctl_stat;
   unsigned long flags;

   /* ======================== */
   local_irq_save(flags);
   spin_lock(&pdev_data->lock);

   ctl_stat = pdev_data->ctl_stat;

   spin_unlock(&pdev_data->lock);
   local_irq_restore(flags);
   /* ======================== */

   switch (ctl_stat){
      case CONTROLLER_TRIGGER_HI:

         /* Send the signal to IO */
         gpio_set_value(pdev_data->gpio.trigger_gpio,1);

         local_irq_save(flags);
         spin_lock(&pdev_data->lock);

         /* kickoff the controller with the trigger_gpio hi flag set 
          * the controller should handle what's next */
         pdev_data->evt_src_flags |= EVENT_SRC_TRG_HI;
         tasklet_schedule (&pdev_data->controller_tasklet);

         spin_unlock(&pdev_data->lock);

         local_irq_restore(flags);

         break;
      case CONTROLLER_TRIGGER_LO:

        /* Send the signal to IO */
         gpio_set_value(pdev_data->gpio.trigger_gpio,0);

         local_irq_save(flags);
         spin_lock(&pdev_data->lock);

          /* we are done sending the trigger_gpio pulse to the gpio 
           * kickoff the controller with the trigger_gpio lo flag set 
          * the controller should handle what's next */
         pdev_data->evt_src_flags |= EVENT_SRC_TRG_LO;
         tasklet_schedule (&pdev_data->controller_tasklet);

         spin_unlock(&pdev_data->lock);

         local_irq_restore(flags);


        break;
      case CONTROLLER_TRIGGERED:

         local_irq_save(flags);
         spin_lock(&pdev_data->lock);

         if ((pdev_data->evt_src_flags & EVENT_SRC_INTERRUPT_RISE) == 0){
            /* timeout has kicked in and that the interrupt flag 
             * has not come back so far
             * kickoff the controller with timeout flag set */
             pdev_data->evt_src_flags |= EVENT_SRC_TIMEOUT;
             tasklet_schedule (&pdev_data->controller_tasklet);
              
         }

         spin_unlock(&pdev_data->lock);
         local_irq_restore(flags);

         break;
      case CONTROLLER_COMPLETED:
      case CONTROLLER_TIMEDOUT:
      case CONTROLLER_INVALID:

         up(&pdev_data->ready_sem);
         break;
      default:
         break;
   }
}

/* Interrupt request handler for GPIO wired to the echo_gpio pin of HCSR04 device */
static irqreturn_t irq_handler(int irq,void* dev_id){
   struct device_data* pdev_data = (struct device_data*)dev_id;
   unsigned long flags;
   irqreturn_t  irqret = IRQ_NONE;

   /* ======================== */
   local_irq_save(flags);
   spin_lock(&pdev_data->lock);
 
   if (pdev_data->gpio.irq_num == irq){
 
      if ((pdev_data->evt_src_flags & EVENT_SRC_INTERRUPT_RISE) == 0){

         /* lets notify the controller that we have received the hardware response
          */
         pdev_data->evt_src_flags |= EVENT_SRC_INTERRUPT_RISE;
         /* fetch the ranging end time
          * This piece of code is very critical to the accuracy of the reading
          * hence handled in the interrupt level*/
         /*pdev_data->range.end_time = current_kernel_time();*/
         getnstimeofday(&pdev_data->range.start_time);


         /* go let the rest of the processing handled by the tasklet */
         tasklet_schedule (&pdev_data->controller_tasklet);

         irqret =  IRQ_HANDLED;
      }
      else if ((pdev_data->evt_src_flags & EVENT_SRC_INTERRUPT_FALL)== 0){
         /* lets notify the controller that we have received the hardware response
          */
         pdev_data->evt_src_flags |= EVENT_SRC_INTERRUPT_FALL;
         /* fetch the ranging end time
          * This piece of code is very critical to the accuracy of the reading
          * hence handled in the interrupt level*/
         /*pdev_data->range.end_time = current_kernel_time();*/
         getnstimeofday(&pdev_data->range.end_time);


         /* go let the rest of the processing handled by the tasklet */
         tasklet_schedule (&pdev_data->controller_tasklet);

         irqret =  IRQ_HANDLED;
         
      }

   }
 
   spin_unlock(&pdev_data->lock);
   local_irq_restore(flags);

   return irqret;
}


