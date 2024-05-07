#!/bin/bash

# run 'candump -l vcanxl0' to create a logfile with the plugfest replay

CANXLGEN=../canxlgen
CIA613FRAG=../cia613frag

TESTIF=vcanxl0
UNFRAGIF=vcanxl1
FS=128

# start fragmenting process with required fragment size
$CIA613FRAG -t 001 -f $FS $UNFRAGIF $TESTIF &
FRAGPID=$!

# create test data frame and segmented traffic
$CANXLGEN $TESTIF -P -S 0 -A 0 -p 401 -l 2048:2048
$CANXLGEN $UNFRAGIF -P -S 0 -A 0 -p 001 -l 2048:2048

sleep 1

# stop fragmenting process with required fragment size
kill $FRAGPID

