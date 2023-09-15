# This launcher script is supposed to replace application calls and forward SIGTERM signals to them
import time
import signal
import os
import sys
import time
import subprocess
import shlex
from multiprocessing import Process

STOP_TIMEOUT = 20
app = None
app_ssh = None
MPI_HOST = None
#MASTER_CMD = "mpiexec --allow-run-as-root -wdir /home/hpc-tests/cm1/ --host " +  str(MPI_HOST) + " -np 4 /home/hpc-tests/cm1/cm1.exe"
WORKER_CMD = "/usr/sbin/sshd -D"

def signal_handler(sig, _frame):
    """Handling the SIGTERM event"""
    print(f'Received signal {sig} - stopping gracefully in 30 seconds')
    os.killpg(os.getpgid(app.pid), sig)
    count = STOP_TIMEOUT
    while count > 0:
        time.sleep(1)
        count -= 1
    print('Finished cleanup...')


def main_worker():
    """Opening subprocesses"""
    global app
    app = subprocess.Popen(shlex.split(WORKER_CMD), preexec_fn=os.setsid)
    signal.signal(signal.SIGTERM, signal_handler)
    app.wait()
    print("Finish execution...")

def main_master():
    """Opening subprocesses"""
    global app
    #global MPI_HOST
    app_ssh = subprocess.Popen("/usr/sbin/sshd", preexec_fn=os.setsid)
    time.sleep(30) # Ensure all workers will be spawned first
    ssh_hosts = open("/etc/volcano/mpiworker.host")
    MPI_HOST = ','.join(line.strip() for line in ssh_hosts)
    os.environ["MPI_HOST"] = MPI_HOST
    MASTER_CMD = "mpiexec --allow-run-as-root -wdir /home/hpc-tests/cm1/ --host " +  str(MPI_HOST) + " -np 4 /home/hpc-tests/cm1/cm1.exe"
    #print("Here are the hosts: ")
    #print(MPI_HOST)
    app = subprocess.Popen(shlex.split(MASTER_CMD), preexec_fn=os.setsid)
    signal.signal(signal.SIGTERM, signal_handler)
    app.wait()
    print("Finish execution...")

if __name__ == "__main__":
    """Defines whether we should follow the master or work router"""
    f = open('/etc/hostname')
    podname = f.read()
    if "worker" in podname: 
        main_worker()
    if "master" in podname:
        main_master()
    else:
        print("Error!")
        exit(0)
