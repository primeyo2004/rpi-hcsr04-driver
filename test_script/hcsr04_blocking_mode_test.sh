#!/bin/bash
# Author: Jeune Prime Origines
# Description: A simple userspace "application" script for testing/demo my HCSR04 device driver 
#              currently uses the block approach where the "read" has to wait until the data is available

exec 3<>"/dev/hcsr04_driver"

COUNTER=1000000 # number of times to start
while [ $COUNTER -gt 0 ]; do
   # write the "start" command to the device file to begin ranging
   echo start >&3
   # read and wait (blocking) for the raw response
   RAWDATA=$(cat <&3)

   # echo the raw data to STDERR
   echo ${RAWDATA} >&2
 
   # parse the response code 0 - success; 
   RESPONSECODE=$(echo  ${RAWDATA} |  sed -e 's/^\(.*\),\(.*\),\(.*\)$/\1/g' | bc)
   DISPLAY_RESULT=""

   case ${RESPONSECODE} in
      0)
         DISTANCE=$(echo  ${RAWDATA} |  sed -e 's/^\(.*\),\(.*\),\(.*\)$/\3/g')
         DISTANCE_FLOAT=$(echo "scale=2; $DISTANCE/100" | bc)
         DISPLAY_RESULT="Status: Success ${DISTANCE_FLOAT} cm."
         ;;
      1)
         DISPLAY_RESULT="Status: In-progress"
         ;;
      2)
         DISPLAY_RESULT="Status: Timedout"
         ;;
      3)
         DISPLAY_RESULT="Status: Not Started"
         ;;
      *)
         DISPLAY_RESULT="Status: Unknown"
         ;;

   esac


   echo ${DISPLAY_RESULT} 


   let COUNTER=COUNTER+1
   sleep 1
done

exec 3>&-                              

