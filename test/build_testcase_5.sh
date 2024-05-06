#!/bin/bash

# run 'candump -l vcanxl0' to create a logfile with the plugfest replay

CANXLGEN=../canxlgen
CIA613FRAG=../cia613frag

TESTIF=vcanxl0
UNFRAGIF=vcanxl1
FS=128

# start fragmenting process with required fragment size
$CIA613FRAG -t 008 -f $FS $UNFRAGIF $TESTIF &
FRAGPID=$!

# create test data frame and segmented traffic
$CANXLGEN $TESTIF -P -S 0 -A 0 -p 408 -l 512:512
$CANXLGEN $UNFRAGIF -P -S 0 -A 0 -p 008 -l 512:512

sleep 1

# stop fragmenting process with required fragment size
kill $FRAGPID
