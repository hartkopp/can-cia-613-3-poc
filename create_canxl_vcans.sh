#!/bin/sh

# Create VCAN CAN XL interfaces with netlink configuration
#
# Author Oliver Hartkopp
#

# Exit if 'ip' from the iproute2 package is not installed
test -x /sbin/ip || exit 0

# On some systems the automatic module loading via
# /etc/modules.conf is quite slow. To ensure the immediately 
# availability of specific modules you can define critical
# modules in the PROBE variable. E.g. PROBE="vcan"

# Exit if modprobe is not installed
test -x /sbin/modprobe || exit 0

# The syntax for the VCAN devices is: devname[:mtu]
# MTU = 16 = sizeof(struct can_frame) for standard CAN2.0B frames (default)
# MTU = 72 = sizeof(struct canfd_frame) for CAN FD frames (Linux 3.6+)
# example VCAN_IF="vcan0:72 vcan1 vcan2 vcan3 helga:72"
VCAN_IF="vcan0 vcan1 vcan2 vcan3 xlsrc:2060 xlfrag:2060 xljoin:2060"
PROBE="vcan"

case "$1" in
	start)
	if [ -n "$PROBE" ] ; then
		echo -n "Extra probing CAN modules:"
		for MODULE in $PROBE; do
			echo -n " "$MODULE
			/sbin/modprobe -q $MODULE
		done
		echo "."
	fi
	if [ -n "$VCAN_IF" ] ; then
		echo -n "Creating and enabling virtual CAN interfaces:"
		for IF in $VCAN_IF; do
			echo -n " "$IF
			DEVICE=${IF%:*}
			/sbin/ip link add name $DEVICE type vcan
			HASMTU=`echo $IF | grep ":"` 
			if [ -n "$HASMTU" ]; then
				MTU=${IF#*:}
				/sbin/ip link set $DEVICE mtu $MTU
			fi
			/sbin/ip link set $DEVICE up
		done
		echo "."
	fi
	;;
	stop)
	if [ -n "$VCAN_IF" ] ; then
		echo -n "Shutting down and removing virtual CAN interfaces:"
		for IF in $VCAN_IF; do
			DEVICE=${IF%:*}
			echo -n " "$DEVICE
			/sbin/ip link set $DEVICE down
			/sbin/ip link del $DEVICE
		done
		echo "."
	fi
	;;
	*)
	echo "Usage: "$0" {start|stop}"
	exit 1
esac

exit 0
