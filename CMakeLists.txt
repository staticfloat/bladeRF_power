cmake_minimum_required(VERSION 2.8.5)
project(bladeRF_power)

# Add internal bladeRF directories
include_directories(${CMAKE_CURRENT_LIST_DIR}/bladeRF/host/libraries/libbladeRF/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/bladeRF/host/common/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/build/bladeRF/common/include)
link_directories(${CMAKE_CURRENT_LIST_DIR}/build/bladeRF/output)

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
add_executable(bladeRF_power
                src/main.cpp
                src/device.cpp
                src/util.cpp
                bladeRF/host/common/src/conversions.c)

# Add libraries
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake/modules)
find_package( ZLIB REQUIRED )
if ( ZLIB_FOUND )
    include_directories( ${ZLIB_INCLUDE_DIRS} )
    target_link_libraries( bladeRF_power ${ZLIB_LIBRARIES} )
else( ZLIB_FOUND )
    error("zlib not found!  Required to build bladeRF_power!")
endif( ZLIB_FOUND )

find_package( FFTW REQUIRED )
if ( FFTW_FOUND )
    include_directories( ${FFTW_INCLUDE_DIRS} )
    target_link_libraries( bladeRF_power ${FFTW_LIBRARIES} )
else( FFTW_FOUND )
    error("FFTW not found!  Required to build bladeRF_power!")
endif( FFTW_FOUND )

target_link_libraries(bladeRF_power bladeRF)

install (TARGETS bladeRF_power DESTINATION bin)
