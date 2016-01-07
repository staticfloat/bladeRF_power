#!/usr/bin/env python

from numpy import *
from scipy import *
from matplotlib.pyplot import *
import sys

if len(sys.argv) > 2 and sys.argv[2] == '-i':
	ion()

z = loadtxt(sys.argv[1], delimiter=",")
if len(z.shape) > 1:
	plot(z[:,0], z[:,1])
else:
	plot(z)
title(sys.argv[1])
show()
if len(sys.argv) > 2 and sys.argv[2] == '-i':
	import IPython
	IPython.embed()
