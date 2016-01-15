cmake_minimum_required(VERSION 2.8.5)
project(bladeRF_power)

# Add internal bladeRF directories
include_directories(${CMAKE_CURRENT_LIST_DIR}/bladeRF/host/libraries/libbladeRF/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/bladeRF/host/common/include)
include_directories(${CMAKE_CURRENT_LIST_DIR}/build/bladeRF/common/include)
link_directories(${CMAKE_CURRENT_LIST_DIR}/build/bladeRF/output)

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
add_executable(bladeRF_power
                src/device.cpp
                src/main.cpp
                src/options.cpp
                src/util.cpp
                src/worker.cpp
                bladeRF/host/common/src/conversions.c)

# Add libraries like FFTW
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake/modules)
find_package(FFTW REQUIRED)
if ( FFTW_FOUND )
    include_directories( ${FFTW_INCLUDE_DIRS} )
    target_link_libraries( bladeRF_power ${FFTW_LIBRARIES} )
else( FFTW_FOUND )
    error("FFTW not found!  Required to build bladeRF_power!")
endif( FFTW_FOUND )

find_package(Threads REQUIRED)
if( Threads_FOUND )
    target_link_libraries( bladeRF_power ${CMAKE_THREAD_LIBS_INIT} )
else( Threads_FOUND )
    error("pthreads not found!  Required to build bladeRF_power!")
endif( Threads_FOUND )

target_link_libraries(bladeRF_power bladeRF)

install (TARGETS bladeRF_power DESTINATION bin)
