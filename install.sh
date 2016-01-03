#!/bin/bash

INSTALL_PREFIX="$(pwd)/prefix"
CMAKE_FLAGS="-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX -DCMAKE_BUILD_TYPE=Debug"
mkdir -p "$INSTALL_PREFIX"

if [[ "$1" == "build" ]]; then
	# Install libbladeRF from source
	if [[ ! -d bladeRF ]]; then
		echo "Downloading bladeRF source..."
		git clone https://github.com/Nuand/bladeRF.git
	fi
	(cd bladeRF; git pull)

	# Clear out prefix, if it's not some global place
	if [[ "$INSTALL_PREFIX" == "$(pwd)/prefix" ]]; then
		rm -rf prefix
	fi

	# Build libbladeRF, if we need to
	mkdir -p build/bladeRF
	(cd build/bladeRF; cmake ../../bladeRF/host $CMAKE_FLAGS && make install)

	# Build bladeRF_power
	mkdir -p build/bladeRF_power
	(cd build/bladeRF_power; cmake ../../ $CMAKE_FLAGS && make install)

	# Download heatmap.py
	#if [[ ! -f heatmap.py ]]; then
	#	curl -L "https://raw.githubusercontent.com/keenerd/rtl-sdr-misc/master/heatmap/heatmap.py" -o heatmap.py
	#	chmod +x ./heatmap.py
	#fi
elif [[ "$1" == "clean" ]]; then
	if [[ "$INSTALL_PREFIX" == "$(pwd)/prefix" ]]; then
		rm -rf prefix
	fi
	rm -rf build bladeRF heatmap.py Vera.ttf
else
	echo "Usage: $0 [command]"
	echo "Where command is one of the following:"
	echo "    build:  Build all prerequisites"
	echo "    clean:  Clean all compiled files"
fi
