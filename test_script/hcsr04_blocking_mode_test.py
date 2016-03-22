#!/usr/bin/python
# Author: Jeune Prime Origines
# Description: A python based simple userspace "application" script for 
#              testing/demo my HCSR04 device driver
#              currently uses the blocked approach where the 'read' has to wait until the data is available from the device

import sys
import re
import time

#open the hcsr04 device
of_dev = open("/dev/hcsr04_driver","r+")

counter = 1000000 # The number of time we need to start the ranging

while counter > 0:
    # write the "start" command to the device to begin ranging
    of_dev.write("start\n")

    # read and wait (blocking) for the raw response
    raw_data = of_dev.read()

    #echo the raw data to the STDERR
    print >> sys.stderr, raw_data

    # parse the response code 0 - success
    mo = re.search(r'^(.+),(.+),(.+)$',raw_data)

    status = 4 # Make Unknown as default status
    status_dict = {0:'Success',
            1:'In-progress',
            2:'Timedout',
            3:'Not Started',
            4:'Unknown'}


    if mo is not None and \
        int(mo.group(1)) in status_dict.keys():
        status = int(mo.group(1))

        if status == 0:
            distance = float(mo.group(3))/100
            print 'Status: %s %0.2f cm.' % (status_dict[status],distance)
    else:
        print 'Status: %s' % (status_dict[status])


    counter -= 1
    time.sleep(1)


file.close()


