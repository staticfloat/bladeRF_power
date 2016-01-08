bladerf_power
===============

Similar in spirit to rtl_power, use this doohickey to do spectrum surveys using your fancy [bladeRF](https://nuand.com/)!

To install easily, run `./install.sh build`.

All other documentation currently pending completion of the C rewrite.


TODO
====

* Get larger frequency margins by calibrating and inverting antialiasing filter

* Implement multiple DC-rejection techniques

* Better thread synchronization in the event of many small buffers, as right now
  thead synchronization doesn't let us
