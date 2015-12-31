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
  -f --file=<f>            File to write to [default: output.csv].
  -z --compress            Compress output with gzip on-the-fly [default: False]
  -e --exit-timer=<et>     Set capture time (example: 5h23m2s) [default: 0]
  -b --bandwidth=<bw>      Capture bandwidth [default: 28M].
  -M --filter-margin=<fm>  Anti-aliasing filter margin [default: .85]. This value
                           is combined with bandwidth to view only a portion of
                           the captured signal to combat leaky anti-aliasing
                           filters. Actual useful signal bandwidth is fm*bw/2.
  -W --window-type=<wt>    Set FFT analysis windowing function [default: hann]
  -g --lna-gain=<g>        Set LNA gain [default: LNA_GAIN_MAX]
  -o --rx-vga1=<g>         Set vga1 gain [default: RXVGA1_GAIN_MIN]
  -w --rx-vga2=<g>         Set vga2 gain [default: RXVGA2_GAIN_MIN]
  -d --device=<d>          Device identifier [default: ]
  -n --num-buffers=<nb>    Number of transfer buffers [default: 16].
  -t --num-transfers=<nt>  Number of transfers [default: 16].
  -l --num-samples=<ns>    Numper of samples per transfer buffer [default: 8192].
  -P --num-workers=<p>     Set number of FFT workers [default: 2]
"""
import sys
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
    import bladeRF
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
    if outfile == '-':
        if compress:
            f = subprocess.Popen("gzip -9 -c", shell=True, stdin=subprocess.PIPE, preexec_fn=os.setpgrp).stdin
        else:
            f = sys.stdout
    else:
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

    if not (outfile == "-" and compress):
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

def analyze_view(data, window_func, lower_sideband, center_freq, analysis_bandwidth, bin_width, start_freq, end_freq, timestamp, q_file):
    """
    Perform power spectral analysis on data of length fft_len, passing it off to
    be written to file afterward.

    Parameters
    ----------
    data : array (real/complex interleaved)
        Incoming SC16 interleaved data samples of length fft_len

    window_func : function
        FFT windowing function, such as scipy.signal.hann()

    lower_sideband : bool
        Whether the lower or upper sideband of this view should be analyzed

    center_freq : float (Hz)
        Center frequency of this view

    analysis_bandwidth : float (Hz)
        The effective bandwidth we analyze in each view

    bin_width : float (Hz)
        The width of each FFT bin, in Hertz

    start_freq : float (Hz)
        The lower edge of the region of interest in frequency

    end_freq : float (Hz)
        The upper edge of the region of interest in frequency

    timestamp : float (seconds)
        The timestamp for this buffer

    q_file : Queue
        Queue used to communicate with file thread
    """
    from numpy.fft import fft

    # Convert data from sc16 into complex data
    data = data[::2] + 1j*data[1::2]

    # Take FFT of this data
    DATA = fft(data * window_func(len(data)))

    # Find start/end frequencies that we get from this FFT, and which bins we
    # want to slice out of the DATA array
    if lower_sideband:
        view_start = max(center_freq - analysis_bandwidth, start_freq)
        view_end = min(center_freq - bin_width, end_freq)

        bin_start = int(round((view_start - center_freq)/bin_width)) + len(DATA)
        bin_end = int(round((view_end - center_freq)/bin_width)) + len(DATA) + 1
    else:
        view_start = max(center_freq + bin_width, start_freq)
        view_end = min(center_freq + analysis_bandwidth, end_freq)

        bin_start = int(round((view_start - center_freq)/bin_width))
        bin_end = int(round((view_end - center_freq)/bin_width)) + 1

    # Prepare data for CSV-ification
    datestr = datetime.datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d, %H:%M:%S')
    csv_str = "%s, %d, %d, %.2f, %d, "%(datestr, view_start, view_end, bin_width, len(data))

    csv_str += ", ".join(["%.2f"%(x) for x in db(DATA[bin_start:bin_end])]) + "\n"

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


def freq_planning(start_freq, end_freq, bin_width, fmbw2):
    """
    Given frequency parameters, returns a list of (center_frequency,
    lower_sideband) tuples, denoting the center frequency of each tuning view,
    and whether we should pay attention to the lower sideband or upper sideband
    when tuning to a particular frequency.

    Parameters
    ----------
    start_freq : float (Hz)
        Beginning of desired frequency range

    end_freq : float (Hz)
        End of desired frequency range, must be greater than start_freq

    bin_width : float (Hz)
        Width of analysis FFT bins

    analysis_bandwidth : float (Hz)
        Width of analysis window in Hz, equal to filter_margin * bandwidth/2

    Returns
    -------
    freqs : list of (center_frequency, lower_sideband) tuples
        A list of views describing a center frequency to tune to and which
        sideband to observe, upper or lower (true signifies lower sideband).
    """
    import bladeRF

    # First frequency is always the same; either just below start_freq or at
    # start_freq + bandwidth/2 - binwidth, in the case that start_freq is really
    # close to the minimum frequency we can tune to:
    if start_freq - bin_width >= bladeRF.FREQUENCY_MIN:
        # Put center_freq just below start_freq if we are not at the minimum frequency
        freqs = [(start_freq - bin_width, False)]
    else:
        # Otherwise, put center_freq just above start_freq + bandwidth/2
        freqs = [(start_freq + fmbw2, True)]

    # Can we get this done with just a single view?
    if end_freq - start_freq < fmbw2:
        return freqs

    # Otherwise, let's figure out how many views we need after the first one
    num_views = int(ceil((end_freq - start_freq)/fmbw2 - 1))
    freqs += [(start_freq + fmbw2 - bin_width + idx*fmbw2, False) for idx in range(num_views)]

    # Return these frequencies!
    return freqs



################################################################################
## MAIN LOOP
################################################################################

def main():
    from time import time, sleep
    import threading
    from Queue import Queue
    import scipy.signal
    import bladeRF
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
    filter_margin = float(args['--filter-margin'])
    start_freq, end_freq, bin_width = [floatish(x) for x in freq_arg.split(':')]
    start_freq = max(start_freq, bladeRF.FREQUENCY_MIN)
    end_freq = min(end_freq, bladeRF.FREQUENCY_MAX)

    if end_freq <= start_freq:
        sys.stderr.write("ERROR: end frequency must be greater than start frequency!\n")
        return

    # fft_len is the minimum length FFT that guarantees us bins of less than or
    # equal width as requested through bin_width:
    fft_len = int(ceil(bandwidth/bin_width))

    # Now that we know our actual fft length, find the true bin width:
    bin_width = bandwidth/fft_len

    # fmbw2 is the amount of spectrum we get with each view, we quantize to our
    # effective bin_width given our bandwidth and number of bins
    fmbw2 = round(filter_margin*(bandwidth/2)*fft_len)/fft_len

    freqs = freq_planning(start_freq, end_freq, bin_width, fmbw2)
    num_views = len(freqs)

    device.lna_gain = int_or_attr(args['--lna-gain'])
    device.rx.vga1 = int_or_attr(args['--rx-vga1'])
    device.rx.vga2 = int_or_attr(args['--rx-vga2'])

    num_buffers = int(args['--num-buffers'])
    num_samples = int(args['--num-samples'])
    num_transfers = int(args['--num-transfers'])

    # Start FFT worker pool
    num_workers = int(args['--num-workers'])
    window_func = getattr(scipy.signal, args['--window-type'])
    outfile = args['--file']
    compress = bool(args['--compress'])
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
        device.rx.frequency = freqs[0][0]
        stream = device.rx.stream(rx_callback, num_buffers, bladeRF.FORMAT_SC16_Q11, num_samples, num_transfers, user_data=rx_data)
        threading.Thread(target=run_stream, args=(stream,)).start()

        sys.stderr.write("Scanning from %sHz to %sHz, using %d views of %sHz with %d bins %sHz wide\n"%(suffixed(start_freq), suffixed(end_freq), num_views, suffixed(fmbw2), fft_len/2, suffixed(bin_width)))

        # Now zoom through frequencies like it's your day off
        freq_idx = 0
        while exit_timer == 0 or curr_time - start_time <= exit_timer:
            samples_received = q_data.get()

            # Now that we have the data, apply it to the pool
            args = (rx_data['data'], window_func, freqs[freq_idx][1], device.rx.frequency, fmbw2, bin_width, start_freq, end_freq, curr_time, q_file)
            pool.apply_async(analyze_view, args)

            # If not, move on to the next frequency
            freq_idx = (freq_idx + 1)%len(freqs)
            device.rx.frequency = freqs[freq_idx][0]
            rx_data['data_idx'] = 0
            if freq_idx == 0:
                curr_time = time()

            total_space = 40
            num_space = int((total_space - 1)*freq_idx/len(freqs))
            tct = time()
            status_line = "[" + " "*num_space + "." + " "*(total_space - num_space - 1) + "]"

            if exit_timer > 0:
                pct_done = "%.1f%%"%(min(100*(tct - start_time)/exit_timer, 100))
            else:
                pct_done = u"\u221e"

            status_line += " %.1fs/%s\r"%(tct - start_time, pct_done)
            sys.stderr.write(status_line)
            sys.stderr.flush()
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
