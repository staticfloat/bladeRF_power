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
  -o --rx-vga1=<g>         Set vga1 gain [default: 0]
  -w --rx-vga2=<g>         Set vga2 gain [default: 0]

  -P --num-workers=<p>     Set number of FFT workers [default: 2]
  -e --exit-timer=<et>     Set amount of time before automatic exit [default: 0]
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



################################################################################
## DATA ANALYSIS
################################################################################
import datetime, sys
from multiprocessing import Process, Manager, Pool
import Queue

def file_worker(q_file, outfile):
    f = open(outfile, 'w')
    while True:
        try:
            data = q_file.get(True, .1)
            if data == "I'm sorry dave, it's time to die":
                break
            print "Got something to write!"
            sys.stdout.flush()
            f.write(data)
        except Queue.Empty:
            pass
        except KeyboardInterrupt:
            break

def start_worker_pool(num_workers, outfile):
    pool = Pool(processes=num_workers+1)
    q_file = Manager().Queue()
    file_process = Process(target=file_worker, args=(q_file, outfile))
    file_process.start()
    return pool, file_process, q_file

def stop_worker_pool(pool, file_process, q_file):
    print "Killing worker pool..."
    try:
        pool.close()
    except:
        print "pool.close() failed"
        pass

    try:
        pool.join()
    except:
        print "pool.join() failed"
        pass

    try:
        q_file.put("I'm sorry dave, it's time to die")
        file_process.join()
    except:
        print "file_process.join() failed"
        pass

def db(x):
    from numpy import log10, abs
    return 20*log10(abs(x))

def analyze_view(data, center_freq, bandwidth, end_freq, timestamp, q_file):
    from numpy.fft import fft, fftshift
    print "Got something to analyze!"
    sys.stdout.flush()

    # Convert data from sc16 into complex data
    data = data[::2] + 1j*data[1::2]

    # Take FFT of this data
    DATA = fftshift(fft(data))

    # Find start/end frequencies that we get from this FFT
    start_freq = center_freq - bandwidth/2
    end_freq = min(start_freq + bandwidth, end_freq)

    # Figure out if we actually want all of the bins in our FFT
    num_bins = len(DATA)
    if start_freq + bandwidth > end_freq:
        num_bins = int(ceil(len(DATA)*((end_freq - start_freq)/bandwidth)))

    # Prepare data for CSV-ification
    datestr = datetime.datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d, %H:%M:%S')
    csv_str = "%s, %d, %d, %.2f, %d"%(datestr, start_freq, end_freq, len(data), num_bins)

    csv_str += ", ".join(["%.2f"%(x) for x in db(DATA[:num_bins])]) + "\n"

    # Send csv_str to file_worker
    q_file.put(csv_str)
    print "Finished processing %d bins, sending out for writing"%(num_bins)
    sys.stdout.flush()




################################################################################
## MAIN LOOP
################################################################################

from time import time, sleep
def main():
    args = get_args()
    device = bladeRF.Device(args['--device'])
    device.rx.enabled = True
    bandwidth = intish(args['--bandwidth'])
    device.rx.bandwidth = bandwidth
    device.rx.sample_rate = intish(args['--bandwidth'])

    freq_arg = args['<lower:upper:bin_width>']
    start_freq, end_freq, bin_width = [floatish(x) for x in freq_arg.split(':')]
    num_views = ceil((end_freq - start_freq)/bandwidth)
    fft_len = int(bandwidth/bin_width)
    freqs = (start_freq + bandwidth/2) + bandwidth * arange(num_views)

    device.lna_gain = getattr(bladeRF, args['--lna-gain'])
    device.rx.vga1 = int(args['--rx-vga1'])
    device.rx.vga2 = int(args['--rx-vga2'])

    num_buffers = int(args['--num-buffers'])
    num_samples = int(args['--num-samples'])
    num_transfers = int(args['--num-transfers'])

    # Start FFT worker pool
    num_workers = int(args['--num-workers'])
    outfile = args['--file']
    pool, file_process, q_file = start_worker_pool(num_workers, outfile)

    # The receiver callback, which fills up queued_data, and sends it on its merry way
    def rx_callback(device, stream, meta_data, samples, num_samples, user_data):
        data = user_data['data']
        idx = user_data['idx']
        in_data = fromstring(stream.current_as_buffer(), dtype=int16)

        if num_samples*2 + idx < fft_len*2:
            # Are we only partially filled?  Then fill in and return
            data[idx:num_samples*2 + idx] = in_data
            user_data['idx'] += num_samples*2
            #print 'Wrote %d samples'%(num_samples*2)
            return stream.next()
        else:
            # Have we filled completely?  Then take what we need from this buffer, and move along
            data[idx:] = in_data[0:(fft_len*2 - idx)]
            #print 'Wrote %d samples and filled her all up (%d)'%((fft_len*2 - idx), fft_len*2)
            return None



    try:
        # We'll use the same timestamp for all frequencies in a single run
        start_time = time()
        curr_time = start_time
        exit_timer = floatish(args['--exit-timer'])
        while curr_time - start_time <= exit_timer:
            data = empty(fft_len*2, dtype=int16)
            rx_data = {
                'data': data,
                'idx': 0,
            }

            stream = device.rx.stream(rx_callback, num_buffers, bladeRF.FORMAT_SC16_Q11, num_samples, num_transfers, user_data=rx_data)
            for center_idx in freqs:
                device.rx.frequency = center_idx
                stream.run()

                # Now that we have all the data, apply it to the pool and move on
                args = (data, device.rx.frequency, device.rx.bandwidth, end_freq, curr_time, q_file)
                pool.apply_async(analyze_view, args)
                sleep(0.15)

            # Update timestamp
            curr_time = time()

    except KeyboardInterrupt:
        stream.join()
    finally:
        stop_worker_pool(pool, file_process, q_file)

    print "done!"

if __name__ == '__main__':
    main()
