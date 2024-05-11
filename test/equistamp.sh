#!/bin/bash

# this script reworks a SocketCAN log file to have an equal gap between
# the CAN frames timestamps

if [ $# -ne 2 ]
then
    echo "missing parameter - try: "$0" <timestep in ms> <file name>"
    exit
fi
STEPMS=$1
FILE=$2

# start timestamp with 0ms
TSMS=0

while read -r TS CANIF CANFRAME COMMENTS
do
    #echo $TS $CANIF $CANFRAME $COMMENTS

    # a timestamp starts with '(' anything else is a comment
    if [[ ${TS:0:1} != "(" ]]
    then
	# only comment detected - print as read
	echo $TS $CANIF $CANFRAME $COMMENTS
	continue
    fi

    # calculate and print the equidistant timestamp
    # bc does not print a leading '0' for values less than 1
    if [ $TSMS -eq 0 ]
    then
	printf "(0.000000) "
    else if [ $TSMS -lt 1000 ]
	 then
	     printf "(0%s) " `echo 'scale=6; '$TSMS'/1000' | bc`
	 else
	     printf "(%s) " `echo 'scale=6; '$TSMS'/1000' | bc`
	 fi
    fi

    # print the CAN interface and other frame content
    echo $CANIF $CANFRAME $COMMENTS

    # increase new timestamp
    TSMS=$(($TSMS + $STEPMS))
done < $FILE

