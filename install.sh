#!/bin/bash

if [[ "$1" == "build" ]]; then
	echo "NOTE: This script will install pybladeRF into your global python directory"
	read -r -p "Is this okay? [y/N] " response
	case $response in
		[yY][eE][sS]|[yY])
			;;
		*)
			exit 0
			;;
	esac

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

