#!/bin/bash

# run 'candump -l vcanxl0' to create a logfile with the plugfest replay

CANXLGEN=../canxlgen
CIA613FRAG=../cia613frag

TESTIF=vcanxl0
UNFRAGIF=vcanxl1
FS=128

# start fragmenting process with required fragment size
$CIA613FRAG -t 031 -f $FS $UNFRAGIF $TESTIF &
FRAGPID=$!

# create test data frame and segmented traffic
$CANXLGEN $TESTIF -P -S 0 -A 0 -p 431 -l 1024:1024
$CANXLGEN $UNFRAGIF -P -S 0 -A 0 -p 031 -l 1024:1024

sleep 1

# stop fragmenting process with required fragment size
kill $FRAGPID

# start fragmenting process with required fragment size
$CIA613FRAG -t 021 -f $FS $UNFRAGIF $TESTIF &
FRAGPID=$!

# create test data frame and segmented traffic
$CANXLGEN $TESTIF -P -S 0 -A 0 -p 421 -l 777:777
$CANXLGEN $UNFRAGIF -P -S 0 -A 0 -p 021 -l 777:777

sleep 1

# stop fragmenting process with required fragment size
kill $FRAGPID

# start fragmenting process with required fragment size
$CIA613FRAG -t 011 -f $FS $UNFRAGIF $TESTIF &
FRAGPID=$!

# create test data frame and segmented traffic
$CANXLGEN $TESTIF -P -S 0 -A 0 -p 411 -l 512:512
$CANXLGEN $UNFRAGIF -P -S 0 -A 0 -p 011 -l 512:512

sleep 1

# stop fragmenting process with required fragment size
kill $FRAGPID

# start fragmenting process with required fragment size
$CIA613FRAG -t 001 -f $FS $UNFRAGIF $TESTIF &
FRAGPID=$!

# create test data frame and segmented traffic
$CANXLGEN $TESTIF -P -S 0 -A 0 -p 401 -l 512:512
$CANXLGEN $UNFRAGIF -P -S 0 -A 0 -p 001 -l 512:512

sleep 1

# stop fragmenting process with required fragment size
kill $FRAGPID
