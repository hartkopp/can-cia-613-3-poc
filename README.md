# can-cia-613-3-poc
PoC for CAN CiA 613-3 LLC Frame Fragmentation

* implementation of CAN XL frame (de)fragmentation
* no CiA 613-3 rx buffer management (in cia613join)
* SEC handling for embedded add-on types (AOT)
* add-on type (AOT) = 1 (001b)
* protocol version = 1 (01b)

### Files

* canxlgen : generate CAN XL traffic with test data
* canxlrcv : display CAN XL traffic (optional: check test data)
* cia613frag : fragment CAN XL frames according to CAN CiA 613-3
* cia613join : join CAN XL frames according to CAN CiA 613-3
* cia613check : CAN CiA 613-3 test application for CiA plugfest 2024-05-16
* create_canxl_vcans.sh : script to create virtual CAN XL interfaces
* test : testcases for hand crafted log files for CiA plugfest 2024-05-16

### PoC test setup and data flow

<img src="https://github.com/hartkopp/can-cia-613-3-poc/blob/main/img/PoC-setup.png" width="800">

### Build the tools

* Just type 'make' to build the tools.
* 'make install' would install the tools in /usr/local/bin (optional)

### Run the PoC

* create virtual CAN XL interfaces with 'create_canxl_vcans.sh start'
  * xlsrc : virtual CAN bus with original (source) CAN XL traffic
  * xlfrag : virtual CAN bus with fragmented CAN XL traffic
  * xljoin : virtual CAN bus with joined/re-assembled CAN XL traffic

* open 4 (optional 5) terminals to run different tools
  1. ./canxlrcv xljoin -P
  2. ./canxlrcv xlfrag (optional)
  3. ./cia613frag xlsrc xlfrag -t 242 -f 128 -v
  4. ./cia613join xlfrag xljoin -t 242 -v 
  5. ./canxlgen xlsrc -p 242 -l 1:2048 -P -v

