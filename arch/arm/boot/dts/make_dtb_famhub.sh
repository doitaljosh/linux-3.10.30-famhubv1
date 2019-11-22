#!/bin/bash

CXX=$CROSS_COMPILE'cpp'
FLAGS='-E -Wp,-MD -nostdinc -undef -D__DTS__ -x assembler-with-cpp'
CAT='cat'
DTC='dtc'
INCLUDEDIR='include'

$CXX $FLAGS -I$INCLUDEDIR SDP1406_FAMHUB_7.dts | $DTC -O dtb -o sdp1406-famhub-7.dtb SDP1406_FAMHUB_7.dts
