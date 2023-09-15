# This launcher script is supposed to replace application calls and forward SIGTERM signals to them
import time
import signal
import os
import threading
import re
import fileinput
import sys
import time
import subprocess
import psutil
import shlex
import shutil
from concurrent import futures
from multiprocessing import Process
import logging
import grpc
import mpi_monitor_pb2
import mpi_monitor_pb2_grpc

STOP_TIMEOUT = 20
app = None
app_ssh = None
MPI_HOST = None
startedRanks = 0
concludedRanks = 0
totalRanks = 0
ended_exec = 0
notdone = 0
chkPt = 0
lock = threading.Lock()

#MASTER_CMD = "mpiexec --allow-run-as-root -wdir /home/hpc-tests/cm1/ --host " +  str(MPI_HOST) + " -np 4 /home/hpc-tests/cm1/cm1.exe"
WORKER_CMD = "/usr/sbin/sshd -D"

#
# Signal handling
#
def signal_handler(sig, _frame):
    """Handling the SIGTERM event"""
    print(f'Received signal {sig} - stopping gracefully in 30 seconds')
    os.killpg(os.getpgid(app.pid), sig)
    count = STOP_TIMEOUT
    while count > 0:
        time.sleep(1)
        count -= 1
    print('Finished cleanup...')
#
# This deals with gRPC protocols
#
class Monitor(mpi_monitor_pb2_grpc.MonitorServicer):
    # This is to react according to how scheduler sends the scaling message to us
    def Scale(self, request, context):
        global app
        global startedRanks
        global chkPt
        global totalRanks
        chkPt = 1
        with lock:
            totalRanks = totalRanks + request.nodes
        
        
        # SIGTERM the app
        os.killpg(os.getpgid(app.pid), signal.SIGTERM)
        # Wait few seconds so app can deal with whatever it needs
        count = 15
        time.sleep(count)
        #os.killpg(os.getpgid(app.pid), signal.SIGKILL) # Forcefully kill it
        #app.wait() # Wait the app to be killed
        checkpoint() # Server checkpoint, application-based
        return mpi_monitor_pb2.Confirmation(confirmMessage='All jobs are stopped, waiting for new replicas!', confirmId=1)

    def checkpointing(self, request, context):
        global chkPt
        chkPt = 2
        return mpi_monitor_pb2.Confirmation(confirmMessage='Checkpointing is confirmed by server!', confirmId=2)

    def JobInit(self, request, context):
        global startedRanks
        with lock:
            startedRanks += 1
        return mpi_monitor_pb2.Confirmation(confirmMessage='Job is confirmed as started!', confirmId=3)

    def RetrieveKeys(self, request, context):
        pubkey = open("/root/.ssh/authorized_keys", "r").read()
        privkey = open("/root/.ssh/id_rsa", "r").read()

        # Append hostip to mpiworker.host
        ssh_hosts = open("/root/mpiworker.host", 'a+')
        ssh_hosts.write("\n" + request.nodeIP)
        ssh_hosts.close()
        return mpi_monitor_pb2.SSHKeys(pubJobKey=pubkey, privJobKey=privkey, confirmId=3)

    def activeServer(self, request, context):
        global chkPt
        global ended_exec
        if chkPt == 1:
            return mpi_monitor_pb2.Confirmation(confirmMessage='Server is active, you need to checkpoint!', confirmId=5)
        if ended_exec == 1:
            return mpi_monitor_pb2.Confirmation(confirmMessage='Server is active, you need to end execution!', confirmId=6)
        else:  
            return mpi_monitor_pb2.Confirmation(confirmMessage='Server is active!', confirmId=4)

    def endExec(self, request, context):
        global concludedRanks
        global notdone
        with lock:
            concludedRanks += 1
        if concludedRanks == getNumberOfRanks():
            notdone = 1
    	#This should be used for telling server that execution is over
        return mpi_monitor_pb2.Confirmation(confirmMessage='Server is active!', confirmId=4)

def getNumberOfRanks():
    with open("/root/mpiworker.host", 'r') as fp:
        length = len(fp.readlines())
    return length

def copyRanks():
    original = "/etc/volcano/mpiworker.host"
    target = "/root/mpiworker.host"
    shutil.copyfile(original, target)
    return 0

def getStartedRanks():
    global startedRanks
    return startedRanks

def wait_signal():
   # Wait all workers to send a message saying that they are active
    global chkPt
    while getStartedRanks() != getNumberOfRanks():
        time.sleep(5)
    return 0

# Check whether process orted exists
def check_process_exists(process_name):
    for proc in psutil.process_iter(['name']):
        if proc.info['name'].startswith(process_name):
            return True
    return False

def confirm_checkpoint():
    with grpc.insecure_channel('grpc-server.default:30173') as channel:
         stub = mpi_monitor_pb2_grpc.MonitorStub(channel)
         response = stub.checkpointing(mpi_monitor_pb2.Dummy22(mtest="hello")) 
    return 0

# Application-specific checkpointing
def checkpoint():
    delimiters = ".", "_"
    regex_pattern = "|".join(map(re.escape, delimiters))
    f = open('/etc/hostname')
    podname = f.read().rstrip('\n')
    # For cm1, files are saved only on mpiworker-0
    if "mpiworker-0" in podname:
        chkpt_path = r'/home/hpc-tests/gromacs/'
        fileList = os.listdir(chkpt_path)
        rstNo = 0
        # Get all the files in the directory
        for file in fileList:
            if "cm1rst" in file:
                b = int(re.split(regex_pattern, file)[1])
                if b > rstNo:
                    rstNo = b
        # Now modify the namelist.input
        line_rstno = " irst      = " + str(rstNo) + ",\n"
        for line in fileinput.input("/home/hpc-tests/cm1/namelist.input", inplace=True):
            if line.strip().startswith('irst'):
                line = line_rstno
            sys.stdout.write(line)
        confirm_checkpoint()
    # Nothing to do on master
    if "master" in podname:
        pass
    return 0

def start_mpi():
    global app
    ssh_hosts = open("/root/mpiworker.host")
    MPI_HOST = ','.join(line.strip() for line in ssh_hosts)
    os.environ["MPI_HOST"] = MPI_HOST
    logs = open("/data/gromacs.txt", "w+")
    error_logs = open("/data/gromacs_error.txt", "w+")
    MASTER_CMD = "mpiexec --allow-run-as-root -wdir /home/hpc-tests/gromacs/ --host " +  str(MPI_HOST) + " -np " + str(getNumberOfRanks()) + " gmx_mpi mdrun -s benchMEM.tpr -ntomp 1 -cpi state.cpt"
    #app = subprocess.Popen(shlex.split(MASTER_CMD), start_new_session=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    app = subprocess.Popen(shlex.split(MASTER_CMD), start_new_session=True, stdout=logs, stderr=error_logs)
    return 0

def get_write_keys(hostip):
    with grpc.insecure_channel('grpc-server.default:30173') as channel:
        stub = mpi_monitor_pb2_grpc.MonitorStub(channel)
        response = stub.RetrieveKeys(mpi_monitor_pb2.nodeName(nodeIP=hostip))
        path = "/root/.ssh/"
        if not os.path.exists(path):
            os.makedirs(path)
        f = open("/root/.ssh/authorized_keys", "w+")
        k = open("/root/.ssh/id_rsa.pub", "w+")
        r = open("/root/.ssh/id_rsa", "w+")
        f.writelines(response.pubJobKey)
        k.writelines(response.pubJobKey)
        r.writelines(response.privJobKey)
        f.close(), k.close(), r.close()
    return 0

def end_exec():
    with grpc.insecure_channel('grpc-server.default:30173') as channel:
        stub = mpi_monitor_pb2_grpc.MonitorStub(channel)
        response = stub.endExec(mpi_monitor_pb2.Dummy22(mtest="hello"))
    return 0  

def nodeIsReady(podname):
    with grpc.insecure_channel('grpc-server.default:30173') as channel:
        stub = mpi_monitor_pb2_grpc.MonitorStub(channel)
        response = stub.JobInit(mpi_monitor_pb2.nodeName(nodeIP=podname))
    return 0  

def check_activity():
    with grpc.insecure_channel('grpc-server.default:30173') as channel:
        stub = mpi_monitor_pb2_grpc.MonitorStub(channel)
        response = stub.activeServer(mpi_monitor_pb2.Dummy22(mtest="orted"))
        if response.confirmId == 4:
            return 0 # Master is active
        if response.confirmId == 5:
            checkpoint() # Master is active but you need to checkpoint
            return 0
        else:
            return 1 # Master is waiting to end execution

def main_worker(podname):
    """Opening subprocesses"""
    global app
    if "scale" in podname: # This means this is a node that was scaled
        hostip = open("/etc/hosts").readlines()[-1].split(sep="\t")[0]
        get_write_keys(hostip)
    
    # Start sshd
    app = subprocess.Popen(shlex.split(WORKER_CMD), start_new_session=True)
    # signal.signal(signal.SIGTERM, signal_handler)
    # app.wait()
    # Send that we are ready to start
    time.sleep(20)
    nodeIsReady(podname)

    # We send signal to server every minute so we know whether we should end or not the application
    end = 0
    while end == 0:
        time.sleep(20)
        end = check_activity()
    # Send a final message to server that we're shutting down the application now
    end_exec()
    print("Finish execution...")
    return 0

def main_master():
    """Opening subprocesses"""
    global app
    global startedRanks
    global concludedRanks
    global ended_exec
    global chkPt
    global totalRanks
    global notdone
    #global MPI_HOST

    app_ssh = subprocess.Popen("/usr/sbin/sshd", start_new_session=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    #time.sleep(30) # Ensure all workers will be spawned first
    port = '30173'
    #port = '50051'
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    mpi_monitor_pb2_grpc.add_MonitorServicer_to_server(Monitor(), server)
    server.add_insecure_port('[::]:' + port)
    server.start()
    print("Server started, listening on " + port)
    
    # Avoid the locked /etc/volcano fs
    copyRanks()
    totalRanks = getNumberOfRanks()

    # Reuse function so everyone can be online
    wait_signal()

    # Reuse the function to restart mpi
    start_mpi()
    print("Application started!")
    concludedRanks = 0

    # We wait application to be done 
    while notdone == 0:
        mpiexec_exists = check_process_exists("mpiexec")
        if mpiexec_exists:
            stdout_app, stderr_app = app.communicate()
        if not mpiexec_exists and chkPt == 0: # MPI app is not active and also we don't need to checkpoint here
            ended_exec = 1 # Execution is over, now wait for all ranks to send message of conclusion
        if not mpiexec_exists and chkPt == 2:
            wait_signal()
            chkPt = 0
            start_mpi() # Restart our mpi job
          #print("Waiting")
        time.sleep(10)

    #app.wait()
    #server.wait_for_termination()
    print("Finishing execution...")
    return 0

if __name__ == "__main__":
    """Defines whether we should follow the master or work router"""
    f = open('/etc/hostname')
    podname = f.read().rstrip('\n')
    if "master" in podname:
        main_master()
    else:
        main_worker(podname)  # This will also work when the pod has not a defined name
