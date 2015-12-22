bladerf_power.py
===============

Similar in spirit to rtl_power, use this doohickey to do spectrum surveys using your fancy [bladeRF](https://nuand.com/)!

To install easily, run `./install.sh build`. I tried to get this to work inside of a `virtualenv` but failed, as `pybladeRF` uses `cffi` which searches for `libbladerf` at runtime, and the virtualenv isolates itself from the rest of the system at runtime.  I am using [my own, patched fork of `pybladeRF`](https://github.com/staticfloat/pybladeRF) and a whole lot of spaghetti code.  This was an afternoon hack, so if there are features you want (most notably, there is no "integration time" option, so all output looks really noisy) feel free to open issues and pull requests.
