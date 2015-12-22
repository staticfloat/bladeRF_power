#!/usr/bin/env python
"""\
bladeRF Receiver
Usage:
  bladerf_power.py <lower:upper:bin_width> [options]
  bladerf_power.py (-h | --help)
  bladerf_power.py --version

Arguments:
  <lower:upper:bin_width>  Frequency sweep parameters, e.g. <900M:1.2G:10K>

Options:
  -h --help                Show this screen.
  -v --version             Show version.
  -d --device=<d>          Device identifier [default: ]
  -f --file=<f>            File to write to [default: output.csv].
  -b --bandwidth=<bw>      Capture bandwidth [default: 28M].
  -n --num-buffers=<nb>    Number of transfer buffers [default: 16].
  -t --num-transfers=<nt>  Number of transfers [default: 16].
  -l --num-samples=<ns>    Numper of samples per transfer buffer [default: 8192].
  -g --lna-gain=<g>        Set LNA gain [default: LNA_GAIN_MAX]
  -o --rx-vga1=<g>         Set vga1 gain [default: RXVGA1_GAIN_MIN]
  -w --rx-vga2=<g>         Set vga2 gain [default: RXVGA2_GAIN_MIN]
  -P --num-workers=<p>     Set number of FFT workers [default: 2]
  -e --exit-timer=<et>     Set capture time (example: 5h23m2s) [default: 0]
  -W --window-type=<wt>    Set window type [default: hann]
  --demean                 Demean signal before taking FFT [default: True]
  -z                       Compress output with gzip on-the-fly [default: False]
"""
import sys
import bladeRF
import IPython
import ipdb
from numpy import *
from docopt import docopt


################################################################################
## OPTION PARSING
################################################################################

def get_args():
    return docopt(__doc__, version='bladerf_power 0.1')

def isdigit(x):
    try:
        int(x)
        return True
    except:
        return False

def suffix(x):
    x = x.lower()
    if x == 'k':
        return 1000
    elif x == 'm':
        return 1000000
    elif x == 'g':
        return 1000000000
    elif x == 't':
        return 1000000000000
    elif x == 'p':
        return 1000000000000000
    elif x == 'e':
        return 1000000000000000000
    else:
        return 1

def suffixed(x):
    tricade = int(log10(abs(x)))/3
    mapping = {0: '', 1:'K', 2:'M', 3:'G', 4:'T', 5:'P', 6:'E'}
    return "%.1f%s"%(x/(1000**tricade), mapping[tricade])

def floatish(x):
    sfx = 1
    if not isdigit(x[-1]):
        sfx = suffix(x[-1])
        x = x[:-1]
    return float(x)*sfx

def intish(x):
    return int(floatish(x))

def int_or_attr(x):
    try:
        return int(x)
    except:
        return getattr(bladeRF, x)

def timeish(x):
    # First off, if this is just an integer with no suffixes, then return it!
    try:
        return int(x)
    except:
        pass

    time_units = {'d':24*60*60, 'h':60*60, 'm':60, 's':1}
    if x[-1] in time_units:
        j = len(x) - 2
        while isdigit(x[j-1]) and j > 0:
            j -= 1
        val = time_units[x[-1]] * intish(x[j:-1])
        if j > 0:
            return val + timeish(x[:j])
        else:
            return val
    return 0

################################################################################
## DATA ANALYSIS
################################################################################
import datetime, sys
from multiprocessing import Process, Manager, Pool
import Queue

def file_worker(q_file, outfile, compress):
    import subprocess
    import os
    if compress:
        f = subprocess.Popen("gzip -9 -c > %s"%(outfile), shell=True, stdin=subprocess.PIPE, preexec_fn=os.setpgrp).stdin
    else:
        f = open(outfile, 'w')

    try:
        while True:
            try:
                data = q_file.get(True, .1)
                if data == "I'm sorry dave, it's time to die":
                    break
                f.write(data)
            except Queue.Empty:
                pass
    except KeyboardInterrupt:
        pass

    f.close()
    print "Gracefully exited file_worker"

def start_worker_pool(num_workers, outfile, compress):
    pool = Pool(processes=num_workers+1)
    q_file = Manager().Queue()
    file_process = Process(target=file_worker, args=(q_file, outfile, compress))
    file_process.start()
    return pool, file_process, q_file

def stop_worker_pool(pool, file_process, q_file):
    print "Closing worker pool..."
    try:
        pool.terminate()
    except:
        print "pool.close() failed"
        pass

    print "Joining worker pool..."
    try:
        pool.join()
    except:
        print "pool.join() failed"
        pass

    print "Joining file process..."
    try:
        q_file.put("I'm sorry dave, it's time to die")
        file_process.join()
    except:
        print "file_process.join() failed"
        pass

def db(x):
    from numpy import log10, abs
    return 20*log10(abs(x))

def analyze_view(data, window_func, demean, center_freq, bandwidth, end_freq, timestamp, q_file):
    from numpy.fft import fft, fftshift

    # Convert data from sc16 into complex data
    data = data[::2] + 1j*data[1::2]

    if demean:
        data = data - mean(data)

    # Take FFT of this data
    DATA = fftshift(fft(data * window_func(len(data))))

    # Find start/end frequencies that we get from this FFT
    start_freq = center_freq - bandwidth/2
    end_freq = min(start_freq + bandwidth, end_freq)

    # Figure out if we actually want all of the bins in our FFT
    num_bins = len(DATA)
    if start_freq + bandwidth > end_freq:
        num_bins = int(ceil(len(DATA)*((end_freq - start_freq)/bandwidth)))

    # Prepare data for CSV-ification
    datestr = datetime.datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d, %H:%M:%S')
    csv_str = "%s, %d, %d, %.2f, %d, "%(datestr, start_freq, end_freq, bandwidth/len(data), len(data))

    csv_str += ", ".join(["%.2f"%(x) for x in db(DATA[:num_bins])]) + "\n"

    # Send csv_str to file_worker
    q_file.put(csv_str)
    sys.stdout.flush()


################################################################################
## RX CALLBACK
################################################################################

# The receiver callback, which fills up queued_data, and sends it on its merry way
def rx_callback(device, stream, meta_data, samples, num_samples, user_data):
    data = user_data['data']
    data_idx = user_data['data_idx']
    fft_len = user_data['fft_len']
    q_data = user_data['q_data']
    in_data = fromstring(stream.current_as_buffer(), dtype=int16)

    # Are we supposed to quit?
    if user_data['running'] == False:
        return None

    # Are we full already?  Then let's just keep on keeping on
    if data_idx == fft_len:
        return stream.current()

    if num_samples*2 + data_idx < fft_len*2:
        # Are we only partially filled?  Then fill in and return
        data[data_idx:num_samples*2 + data_idx] = in_data
        user_data['data_idx'] += num_samples*2
        return stream.next()
    else:
        # Have we filled completely?  Then take what we need from this buffer, and discard the rest
        data[data_idx:] = in_data[0:(fft_len*2 - data_idx)]
        user_data['data_idx'] = fft_len
        q_data.put(fft_len)
        return stream.next()


################################################################################
## MAIN LOOP
################################################################################

def main():
    from time import time, sleep
    import threading
    from Queue import Queue
    import scipy.signal
    args = get_args()
    try:
        device = bladeRF.Device(args['--device'])
    except:
        if args['--device'] == '':
            print "ERROR: No bladeRF devices available!"
        else:
            print "ERROR: Could not open bladeRF device %s"%(args['--device'])
        return
    device.rx.enabled = True
    bandwidth = intish(args['--bandwidth'])
    device.rx.bandwidth = bandwidth
    device.rx.sample_rate = intish(args['--bandwidth'])

    freq_arg = args['<lower:upper:bin_width>']
    start_freq, end_freq, bin_width = [floatish(x) for x in freq_arg.split(':')]
    start_freq = max(start_freq, bladeRF.FREQUENCY_MIN)
    end_freq = max(start_freq, bladeRF.FREQUENCY_MAX)

    num_freqs = ceil((end_freq - start_freq)/bandwidth)
    fft_len = int(ceil(bandwidth/bin_width))
    freqs = (start_freq + bandwidth/2) + bandwidth * arange(num_freqs)
    # Clamp freqs because we may attempt to overstep our bounds due to the ceil
    # above when calculating num_freqs.  This is okay, usually, except when we
    # exceed FREQUENCY_MAX.
    if freqs[-1] > bladeRF.FREQUENCY_MAX:
        freqs[-1] = bladeRF.FREQUENCY_MAX

    device.lna_gain = int_or_attr(args['--lna-gain'])
    device.rx.vga1 = int_or_attr(args['--rx-vga1'])
    device.rx.vga2 = int_or_attr(args['--rx-vga2'])

    num_buffers = int(args['--num-buffers'])
    num_samples = int(args['--num-samples'])
    num_transfers = int(args['--num-transfers'])

    # Start FFT worker pool
    num_workers = int(args['--num-workers'])
    window_func = getattr(scipy.signal, args['--window-type'])
    demean = bool(args['--demean'])
    outfile = args['--file']
    compress = bool(args['-z'])
    if compress and outfile[-3:] != '.gz':
        outfile += '.gz'
    pool, file_process, q_file = start_worker_pool(num_workers, outfile, compress)

    # Timing stuffage
    start_time = time()
    curr_time = start_time
    exit_timer = timeish(args['--exit-timer'])

    # This is the thread that runs the stream.  So exciting, la
    def run_stream(stream):
        stream.run()

    try:
        q_data = Queue()
        rx_data = {
            'data': empty(fft_len*2, dtype=int16),
            'data_idx': 0,
            'fft_len': fft_len,
            'q_data': q_data,
            'running': True
        }

        # Initialize device.rx.frequency, then start the stream doing its thing
        device.rx.frequency = freqs[0]
        stream = device.rx.stream(rx_callback, num_buffers, bladeRF.FORMAT_SC16_Q11, num_samples, num_transfers, user_data=rx_data)
        threading.Thread(target=run_stream, args=(stream,)).start()

        # Now zoom through frequencies like it's your day off
        freq_idx = 0
        while curr_time - start_time <= exit_timer:
            samples_received = q_data.get()

            # Now that we have the data, apply it to the pool
            args = (rx_data['data'], window_func, demean, device.rx.frequency, device.rx.bandwidth, end_freq, curr_time, q_file)
            pool.apply_async(analyze_view, args)

            # If not, move on to the next frequency
            freq_idx = (freq_idx + 1)%len(freqs)
            device.rx.frequency = freqs[freq_idx]
            rx_data['data_idx'] = 0
            if freq_idx == 0:
                curr_time = time()

            total_space = 40
            num_space = int((total_space - 1)*freq_idx/len(freqs))
            tct = time()
            status_line = "[" + " "*num_space + "." + " "*(total_space - num_space - 1) + "]"
            status_line += " %.1fs/%.1f%%\r"%(tct - start_time, min(100*(tct - start_time)/exit_timer, 100))
            sys.stdout.write(status_line)
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        print # Clear out the status_line stuffage
        rx_data['running'] = False
        stop_worker_pool(pool, file_process, q_file)
        print "Done stopping the worker pool!"

    print "done!"

if __name__ == '__main__':
    main()
