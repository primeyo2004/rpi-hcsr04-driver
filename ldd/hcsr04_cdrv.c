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
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/ctype.h>
#include "hcsr04_async_device.h"
/* This code is written for Rasberry PI 2 */

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A Linux device driver for HC-SR04 Ultrasonic sensor interfaced with Raspberry PI 2 GPIO");
MODULE_AUTHOR("Jeune Prime Origines");



static int driver_entry(void);
static void driver_exit(void);
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);


static unsigned int  param_trigger_gpio = 17;
static unsigned int  param_echo_gpio    = 18;
static unsigned int  param_usec_pulse_width = 10;  /* 10 ms */
static unsigned int  param_usec_timeout = 300000;  /* 300 ms */

static dev_t dev_num = 0;
struct cdev *mcdev = NULL;
extern char DEVICE_NAME[];

static const char start_cmd[] = "start";

static struct semaphore instance_sem;


static struct file_operations fops = {
   .owner = THIS_MODULE,
   .read = device_read,
   .write = device_write,
   .open = device_open,
   .release = device_release
};


/* Module Entry and Exit functions */
module_init(driver_entry);
module_exit(driver_exit);


static int driver_entry(void){
   int result = SUCCESS;

   if ((result  = alloc_chrdev_region(&dev_num,0,1,DEVICE_NAME)) < SUCCESS) {
      printk (KERN_ALERT "%s: Failed to allocate character device number.\n",DEVICE_NAME);
      goto func_exit;
   }
   
         
   if ((mcdev = cdev_alloc()) == NULL){
      result = -ENODEV;
      printk(KERN_ALERT "%s: Unable to allocate device memory\n",DEVICE_NAME);
      goto func_exit;
   }

   mcdev->ops = &fops;
   mcdev->owner = THIS_MODULE;

   if ((result = cdev_add(mcdev,dev_num,1)) < SUCCESS){
      printk(KERN_ALERT "%s: Unable to add cdev to kernel\n",DEVICE_NAME);
      return result;
   }

   // only one instance supported for now
   sema_init(&instance_sem,1);

   printk(KERN_INFO "%s: Initialization success with major number = %d!\n",DEVICE_NAME,MAJOR(dev_num));

func_exit:
   if (result != SUCCESS){

      if (mcdev != NULL){
         cdev_del(mcdev);
         mcdev = NULL;
      }
      
      if (dev_num != 0){
         unregister_chrdev_region(dev_num,1);
         dev_num = 0;
      }
   }
   return result;  
}

static void driver_exit(void){
   cdev_del(mcdev);
   unregister_chrdev_region(dev_num,1);
   printk(KERN_INFO "%s: Device is uninitialized\n",DEVICE_NAME);
}




/* File Operation Functions */
static int device_open(struct inode *inode, struct file *file)
{
   int retval; 

   if (down_trylock(&instance_sem)){
      printk (KERN_ALERT "%s: Device is currently in use!\n",DEVICE_NAME);
      retval = -EBUSY;
      goto exit_func;
   }

   if ((retval = init_ranging_device(param_trigger_gpio,
         param_echo_gpio,
         param_usec_pulse_width,
         param_usec_timeout,
         true,
         &file->private_data)) != SUCCESS){

      printk (KERN_ALERT "%s: Opening device failed with error: %d\n",DEVICE_NAME,retval);
      goto exit_func;
      
   }
exit_func:
   if (retval == SUCCESS){
      printk (KERN_INFO "%s: Open success\n",DEVICE_NAME);
   }
   return retval;
}

static int device_release(struct inode *inode, struct file *file)
{
   release_ranging_device(file->private_data);
   up(&instance_sem);
   return SUCCESS;
}

static ssize_t device_read(struct file *filp,	/* see include/linux/fs.h   */
			   char *buffer,	/* buffer to fill with buffer */
			   size_t length,	/* length of the buffer     */
			   loff_t * offset)
{
   int retval = SUCCESS;
   static char data_buffer[100];
   static ranging_result_t result_code;
   static struct timespec start_time;
   static struct timespec end_time;
   static struct timespec delta_time;

   if ((retval = read_async_ranging_result(
               filp->private_data,
               &result_code,
               &start_time,
               &end_time,
               &delta_time)) != SUCCESS){
      printk (KERN_ALERT "%s: Failed to read the ranging device!\n",DEVICE_NAME);
      goto exit_func;
   }
   

   if ( result_code  == RRESULT_NOT_STARTED ){
      retval  =0;
      goto exit_func;
   }

   if ((retval = reset_async_ranging(filp->private_data)) != SUCCESS){
      printk (KERN_ALERT "%s: Failed to reset the ranging device!\n",DEVICE_NAME);
      goto exit_func;
   }

   sprintf(data_buffer,"%d,%ld:%ld,%ld:%ld,%ld:%ld,%ld\n",
         (int)result_code,
         start_time.tv_sec,
         start_time.tv_nsec,
         end_time.tv_sec,
         end_time.tv_nsec,
         delta_time.tv_sec,
         delta_time.tv_nsec,
         (delta_time.tv_nsec*100) / 58140);

   printk(KERN_INFO "%s:%s\n",DEVICE_NAME,data_buffer);

   
   retval = strlen(data_buffer);
   if (length < retval || copy_to_user(buffer,data_buffer,retval) != SUCCESS){
      printk (KERN_ALERT "%s: Read buffer is insufficient!\n",DEVICE_NAME);

      retval = -ENOBUFS;
   }


exit_func:
   return retval;
}

static ssize_t
device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
   int oldlen = len;
   int retval  = SUCCESS;  
      
   const char *p  = start_cmd;
   char  c_user;

   /* all we need is that at least the first word in the 
    * buffer is "start" not case sensitive*/
   while (len && !get_user(c_user,buff) && isspace(c_user)){
      len--;
      buff++;
   }

   while (len && !get_user(c_user,buff) && *p == tolower(c_user)){
      p++;
      buff++;
      len--;
   }

   if (*p != '\0' || (c_user != '\0' &&  !isspace(c_user) && len !=0 )){
      retval =  -EINVAL;
      printk (KERN_ALERT "%s: Invalid device command!\n",DEVICE_NAME);
      goto exit_func;
   } 

   if ((retval = start_async_ranging (filp->private_data)) != SUCCESS){

      printk (KERN_ALERT "%s: Failed to start device ranging!\n",DEVICE_NAME);
      goto exit_func;
   }

exit_func:
   return  (retval == SUCCESS ? oldlen:retval);
}




