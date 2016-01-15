#!/bin/bash

PREFIX="$(pwd)/prefix"
CMAKE_DFLAGS="-DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_BUILD_TYPE=Debug"
CMAKE_FLAGS="-DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_BUILD_TYPE=Release"
mkdir -p "$PREFIX"

function brew_install()
{
	if [[ "$(brew info "$1" 2>/dev/null)" == *"Not installed"* ]]; then
		echo "Running brew install \"$1\"..."
		brew install "$1"
	fi
}

function aptget_install()
{
	if [[ "$(dpkg -s "$1" 2>/dev/null)" != *"Status: install ok installed"* ]]; then
		echo "Running sudo apt-get install \"$1\"..."
		sudo apt-get install "$1"
	fi
}

function install_libusb()
{
	if [[ "$(uname -s)" == "Linux" ]]; then
		if [[ ! -z "$(which apt-get)" ]]; then
			aptget_install libusb-1.0-0-dev
			aptget_install libfftw3-dev
			aptget_install build-essential
			aptget_install cmake
			aptget_install libncurses5-dev
			aptget_install libtecla
			aptget_install pkg-config
			aptget_install git
			aptget_install wget
		else
			echo "I don't know how to install dependencies on a Linux system without apt-get!"
			exit 1
		fi
	elif [[ "$(uname -s)" == "Darwin" ]]; then
		if [[ ! -z "$(which brew)" ]]; then
			brew_install pkg-config
			brew_install libusb
			brew_install fftw
			brew_install git
			brew_install libtecla
			brew_install wget
			brew_install cmake
		else
			echo "I don't know how to install libusb on an OSX system without brew!"
			exit 1
		fi
	fi
}

if [[ "$1" == "build" ]] || [[ "$1" == "build/" ]]; then
	echo "Installing into $PREFIX..."

	# Install libbladeRF from source
	if [[ ! -d bladeRF ]]; then
		echo "Downloading bladeRF source..."
		git clone https://github.com/Nuand/bladeRF.git
	fi
	(cd bladeRF; git pull)

	echo "Ensuring libusb is installed..."
	install_libusb

	# Clear out prefix, if it's not some global place
	if [[ "$PREFIX" == "$(pwd)/prefix" ]]; then
		rm -rf prefix
	fi
	mkdir -p "$PREFIX/bin"

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

		echo "Patching heatmap.py to allow fractional timestamps..."
		sed -i "" "s/fromtimestamp(int(s))/fromtimestamp(float(s))/g" heatmap.py
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
	rm -rf build bladeRF heatmap.py flatten.py Vera.ttf
else
	echo "Usage: $0 [command]"
	echo "Where command is one of the following:"
	echo "    build:  Build all prerequisites"
	echo "    clean:  Clean all compiled files"
fi
