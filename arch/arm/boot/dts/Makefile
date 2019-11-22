##################################
#
# DTB Makefile
#
##################################

CXX = $(CROSS_COMPILE)g++
DTC = ../dtc/dtc
FLAGS = $(CPPFLAGS) -E -Wp,-MD -nostdinc -undef -D__DTS__ -x assembler-with-cpp
INCLDUEDIR = include
CAT = cat

ifneq ($2,"")
MODE=$2
endif

all: sdp1304-dtv-fhd.dtb sdp1304-dtv-fhd-1_5g.dtb \
     sdp1404-tizen.dtb sdp1304-dtv-fhd-gptizen.dtb \
     sdp1406-tizen.dtb 

.PHONY: sdp1304-dtv-fhd.dtb sdp1304-dtv-fhd-1_5g.dtb \
	sdp1404-tizen.dtb sdp1304-dtv-fhd-gptizen.dtb \
	sdp1406-tizen.dtb

sdp1304-dtv-fhd.dtb: sdp1304-dtv-fhd.dts
	$(CAT) sdp1304-dtv-fhd.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.prehawk.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1304-dtv-fhd.dtb - && rm -rf "-"

sdp1304-dtv-fhd-1_5g.dtb: sdp1304-dtv-fhd-1_5g.dts
	$(CAT) sdp1304-dtv-fhd-1_5g.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.prehawk.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1304-dtv-fhd-1_5g.dtb - && rm -rf "-"

sdp1404-tizen.dtb: sdp1404-tizen.dts
	$(CAT) sdp1404-tizen.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.hawkp.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1404-tizen.dtb - && rm -rf "-"

sdp1404-sbb.dtb: sdp1404-sbb.dts
	$(CAT) sdp1404-sbb.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.hawkp.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1404-sbb.dtb - && rm -rf "-"

sdp1404-sbb-2.5G.dtb: sdp1404-sbb-2.5G.dts
	$(CAT) sdp1404-sbb-2.5G.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.hawkp.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1404-sbb-2.5G.dtb - && rm -rf "-"

sdp1304-dtv-fhd-gptizen.dtb: sdp1304-dtv-fhd-gptizen.dts
	$(CAT) sdp1304-dtv-fhd-gptizen.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.prehawk.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1304-dtv-fhd-gptizen.dtb - && rm -rf "-"

sdp1406-FHD-1G-tizen.dtb: sdp1406-FHD-1G-tizen.dts
	$(CAT) sdp1406-FHD-1G-tizen.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.hawkm-fhd.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1406-FHD-1G-tizen.dtb - && rm -rf "-"

sdp1406-MTV-1G-tizen.dtb: sdp1406-MTV-1G-tizen.dts
	$(CAT) sdp1406-MTV-1G-tizen.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.hawkm-mtv.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1406-MTV-1G-tizen.dtb - && rm -rf "-"

sdp1406-FHD-1.5G-ATSC-tizen.dtb: sdp1406-FHD-1.5G-ATSC-tizen.dts
	$(CAT) sdp1406-FHD-1.5G-ATSC-tizen.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.hawkm-fhd.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1406-FHD-1.5G-ATSC-tizen.dtb - && rm -rf "-"

sdp1406-FHD-1.5G-DVB-tizen.dtb: sdp1406-FHD-1.5G-DVB-tizen.dts
	$(CAT) sdp1406-FHD-1.5G-DVB-tizen.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.hawkm-fhd.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1406-FHD-1.5G-DVB-tizen.dtb - && rm -rf "-"

sdp1406-UHD-1.5G-tizen.dtb: sdp1406-UHD-1.5G-tizen.dts
	$(CAT) sdp1406-UHD-1.5G-tizen.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.hawkm-uhd.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1406-UHD-1.5G-tizen.dtb - && rm -rf "-"

sdp1406-UHD-2G-tizen.dtb: sdp1406-UHD-2G-tizen.dts
	$(CAT) sdp1406-UHD-2G-tizen.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.hawkm-uhd.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1406-UHD-2G-tizen.dtb - && rm -rf "-"

sdp1406-UHD-2.5G-tizen.dtb: sdp1406-UHD-2.5G-tizen.dts
	$(CAT) sdp1406-UHD-2.5G-tizen.dts | sed -e s/__DTS_VERSION__/`head -n1 ../version.hawkm-uhd.txt`_$(basename $<)/g | $(CXX) $(FLAGS) -I$(INCLDUEDIR) - | $(DTC) -O dtb -o sdp1406-UHD-2.5G-tizen.dtb - && rm -rf "-"

clean:
	rm -rf *.dtb
