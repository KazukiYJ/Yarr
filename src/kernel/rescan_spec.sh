#!/bin/sh

SEARCH_WORD="Xilinx"

DEVICES=`lspci | grep $SEARCH_WORD | sed -e "s/\ .*//g" | xargs` # Remove after first space
DEVICE_NUM=`echo $DEVICES | wc -w`

if [ $DEVICE_NUM -eq 0 ]; then
	echo "WARNING : Not found spec devices." >&2
	echo "Try rescanning?" >&2
	select CHOICE in "YES" "NO"; do
		if [ "$CHOICE" = "YES" ]; then
			echo "Rescanning..."
			echo "1" > /sys/bus/pci/rescan
			echo "-----> Done!"
			exit 0
		else
			echo "ByeBye!" >&2
			exit 1
		fi
	done

elif [ $DEVICE_NUM -eq 1 ]; then
	DEVICE=$DEVICES
	echo "1 device ($DEVICE) is detected."
elif [ $DEVICE_NUM -ge 2 ]; then
	echo "Multiple devices are detected."
	echo "Choose a device to remove/rescan."
	select DEVICE in $DEVICES; do
		if [ -z "$DEVICE" ]; then
			echo "WARNING : Invalid choice." >&2
		else
			echo "You chose $DEVICE"
			break
		fi
	done
else
	echo "ERROR : Unknown error." >&2
	exit 1
fi

echo "Removing/Rescanning the device ($DEVICE)."
echo "1" > /sys/bus/pci/devices/0000:${DEVICE}/remove
echo "1" > /sys/bus/pci/rescan
# ls -l /sys/bus/pci/devices/0000:${DEVICE}

sleep 1

if [ -e /sys/bus/pci/devices/0000:${DEVICE} ]; then
	echo "-----> Done!"
	exit 0
else
	echo "ERROR : Failed to rescan."
	exit 1
fi
