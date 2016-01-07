#!/usr/bin/env python

from numpy import *
from scipy import *
from matplotlib.pyplot import *
import sys

if len(sys.argv) > 2 and sys.argv[2] == '-i':
	ion()

x = loadtxt(sys.argv[1], delimiter=",")
z = x[::2] + 1j*x[1::2]
plot(abs(z))
title(sys.argv[1])

figure()
plot(20*log10(abs(fft(z))[:len(z)/2]))
title("dB FFT")
show()
if len(sys.argv) > 2 and sys.argv[2] == '-i':
	import IPython
	IPython.embed()
