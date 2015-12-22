#!/bin/bash

if [[ "$1" == "build" ]]; then
	echo "NOTE: This script will install pybladeRF and libbladeRF globally."
	read -r -p "Is this okay? [y/N] " response
	case $response in
		[yY][eE][sS]|[yY])
			;;
		*)
			exit 0
			;;
	esac

	# Install libbladeRF
	if [[ $(uname -s) == "Darwin" ]]; then
		if [[ -z $(which brew) ]]; then
			echo "brew not found, blithely assuming you have already installed libbladerf appropriately!"
		else
			brew install libbladerf
		fi
	else
		echo "I don't know how to install libbladerf on this system, blithely assuming you have already installed it!"
	fi

	# Install pybladeRF
	git clone https://github.com/staticfloat/pybladeRF.git
	(cd pybladeRF; python setup.py install)

	# Download heatmap.py
	if [[ ! -f heatmap.py ]]; then
		curl -L "https://raw.githubusercontent.com/keenerd/rtl-sdr-misc/master/heatmap/heatmap.py" -o heatmap.py
		chmod +x ./heatmap.py
	fi
elif [[ "$1" == "clean" ]]; then
	rm -rf pybladeRF build *.pyc
else
	echo "Usage: $0 [command]"
	echo "Where command is one of the following:"
	echo "    build:  Build all prerequisites"
	echo "    clean:  Clean all compiled files"
fi

