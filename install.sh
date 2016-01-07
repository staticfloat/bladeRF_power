#!/bin/bash

PREFIX="$(pwd)/prefix"
CMAKE_DFLAGS="-DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_BUILD_TYPE=Debug"
CMAKE_FLAGS="-DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_BUILD_TYPE=Release"
mkdir -p "$PREFIX"

if [[ "$1" == "build" ]]; then
	echo "Installing into $PREFIX..."

	# Install libbladeRF from source
	if [[ ! -d bladeRF ]]; then
		echo "Downloading bladeRF source..."
		git clone https://github.com/Nuand/bladeRF.git
	fi
	(cd bladeRF; git pull)

	# Clear out prefix, if it's not some global place
	if [[ "$PREFIX" == "$(pwd)/prefix" ]]; then
		rm -rf prefix
	fi

	# Build libbladeRF, if we need to
	mkdir -p build/bladeRF
	(cd build/bladeRF; cmake ../../bladeRF/host $CMAKE_FLAGS && make install)

	# Build bladeRF_power
	mkdir -p build/bladeRF_power_debug build/bladeRF_power_release
	(cd build/bladeRF_power_debug; cmake ../../ $CMAKE_DFLAGS && make install)
	(cd build/bladeRF_power_release; cmake ../../ $CMAKE_FLAGS && make install)

	# Download heatmap and flatten.py
	if [[ ! -f ./heatmap.py ]]; then
		curl -L "https://raw.githubusercontent.com/keenerd/rtl-sdr-misc/master/heatmap/heatmap.py" -o ./heatmap.py
		chmod +x ./heatmap.py
	fi

	if [[ ! -f ./flatten.py ]]; then
		curl -L "https://raw.githubusercontent.com/keenerd/rtl-sdr-misc/master/heatmap/flatten.py" -o ./flatten.py
		chmod +x flatten.py
	fi

	# link heatmap and flatten.py into our prefix
	if [[ ! -f "$PREFIX/bin/heatmap.py" ]]; then
		ln -s "$(pwd)/heatmap.py" "$PREFIX/bin/heatmap.py"
	fi
	if [[ ! -f "$PREFIX/bin/flatten.py" ]]; then
		ln -s "$(pwd)/flatten.py" "$PREFIX/bin/flatten.py"
	fi
elif [[ "$1" == "clean" ]]; then
	if [[ "$PREFIX" == "$(pwd)/prefix" ]]; then
		rm -rf prefix
	fi
	rm -rf build bladeRF heatmap.py Vera.ttf
else
	echo "Usage: $0 [command]"
	echo "Where command is one of the following:"
	echo "    build:  Build all prerequisites"
	echo "    clean:  Clean all compiled files"
fi
