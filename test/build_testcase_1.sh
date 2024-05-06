#!/bin/bash

# run 'candump -l vcanxl0' to create a logfile with the plugfest replay

CANXLGEN=../canxlgen
CIA613FRAG=../cia613frag

TESTIF=vcanxl0
UNFRAGIF=vcanxl1

for FS in 128 512 1024; do
    # start fragmenting process with required fragment size
    $CIA613FRAG -t 001 -f $FS $UNFRAGIF $TESTIF &
    FRAGPID=$!

    # create test data frame and segmented traffic
    $CANXLGEN $TESTIF -P -S 0 -A 0 -p 401 -l 1:1
    $CANXLGEN $UNFRAGIF -P -S 0 -A 0 -p 001 -l 1:1

    sleep 1

    $CANXLGEN $TESTIF -P -S 0 -A 0 -p 401 -l 2048:2048
    $CANXLGEN $UNFRAGIF -P -S 0 -A 0 -p 001 -l 2048:2048

    sleep 1

    $CANXLGEN $TESTIF -P -S 0 -A 0 -p 401 -l 511:511
    $CANXLGEN $UNFRAGIF -P -S 0 -A 0 -p 001 -l 511:511

    sleep 1

    $CANXLGEN $TESTIF -P -S 0 -A 0 -p 401 -l 512:512
    $CANXLGEN $UNFRAGIF -P -S 0 -A 0 -p 001 -l 512:512

    sleep 1

    $CANXLGEN $TESTIF -P -S 0 -A 0 -p 401 -l 513:513
    $CANXLGEN $UNFRAGIF -P -S 0 -A 0 -p 001 -l 513:513

    sleep 1

    # stop fragmenting process with required fragment size
    kill $FRAGPID
done

