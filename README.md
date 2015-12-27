bladerf_power.py
===============

Similar in spirit to rtl_power, use this doohickey to do spectrum surveys using your fancy [bladeRF](https://nuand.com/)!

To install easily, run `./install.sh build`. I tried to get this to work inside of a `virtualenv` but failed, as `pybladeRF` uses `cffi` which searches for `libbladerf` at runtime, and the virtualenv isolates itself from the rest of the system at runtime.  I am using [my own, patched fork of `pybladeRF`](https://github.com/staticfloat/pybladeRF) and a whole lot of spaghetti code.  This was an afternoon hack, so if there are features you want (most notably, there is no "integration time" option, so all output looks really noisy) feel free to open issues and pull requests.

Example usage (after installation):
```
$ ./bladerf_power.py 300M:3.7G:10k -e 4h -f output.csv
$ ./heatmap.py output.csv output.png
```

Note that wide, dense sweeps of the spectrum such as the one described above can take a lot of diskspace; use `-z` to gzip-on-the-fly to save those precious, precious bits.  As an anecdotal example, the above command generates a 5.6GB .csv file which compresses down to a 1.6GB .csv.gz file.  Either way, `heatmap.py` takes over two hours to crank through that beauty on my machine.

Note that if you see a bunch of scary, strong lines every 28MHz, this probably means that you need to [calibrate your bladeRF](https://github.com/Nuand/bladeRF/wiki/DC-offset-and-IQ-Imbalance-Correction).  Also try adding the `--demean` option to `bladerf_power.py`.
