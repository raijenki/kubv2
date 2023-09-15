# This launcher script is supposed to replace application calls and forward SIGTERM signals to them
import time
import signal
import os
import sys
import subprocess
from multiprocessing import Process

STOP_TIMEOUT = 10
stream = None

def signal_handler(sig, _frame):
    """Handling the SIGTERM event"""
    print(f'Received signal {sig} - stopping gracefully in 30 seconds')
    os.killpg(os.getpgid(stream.pid), sig)
    count = STOP_TIMEOUT
    while count > 0:
        time.sleep(1)
        count -= 1
    print('Finished cleanup...')


def main():
    """Opening subprocesses"""
    global stream
    stream = subprocess.Popen(["/home/stream_c"] + sys.argv[1:], preexec_fn=os.setsid)
    signal.signal(signal.SIGTERM, signal_handler)
    stream.wait()
    print("Finish execution...")

if __name__ == "__main__":
    main()
