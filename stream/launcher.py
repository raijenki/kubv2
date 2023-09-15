# This launcher script is supposed to replace application calls and forward SIGTERM signals to them
import time
import signal
import os
import sys
import subprocess
from multiprocessing import Process
from concurrent import futures

import grpc
import mpi_monitor_pb2
import mpi_monitor_pb2_grpc

STOP_TIMEOUT = 10
stream = None

class Monitor(mpi_monitor_pb2_grpc.MonitorServicer):
    # This is to react according to how scheduler sends the scaling message to us
    def Scale(self, request, context):
        global stream
        os.killpg(os.getpgid(stream.pid), signal.SIGTERM)
        checkpoint()
        return mpi_monitor_pb2.Confirmation(confirmMessage='All jobs are stopped, waiting for new replicas!', confirmId=1)

    def checkpointing(self, request, context):
        return mpi_monitor_pb2.Confirmation(confirmMessage='Checkpointing is confirmed by server!', confirmId=2)

    def JobInit(self, request, context):
        return mpi_monitor_pb2.Confirmation(confirmMessage='Job is confirmed as started!', confirmId=3)

    def RetrieveKeys(self, request, context):
        pubkey="0"
        privkey="1"
        return mpi_monitor_pb2.SSHKeys(pubJobKey=pubkey, privJobKey=privkey, confirmId=3)

    def activeServer(self, request, context):
        return mpi_monitor_pb2.Confirmation(confirmMessage='Server is active!', confirmId=4)

    def endExec(self, request, context):
        return mpi_monitor_pb2.Confirmation(confirmMessage='Server is active!', confirmId=4)

def signal_handler(sig, _frame):
    """Handling the SIGTERM event"""
    print(f'Received signal {sig} - stopping gracefully in 10 seconds')
    os.killpg(os.getpgid(stream.pid), sig)
    count = STOP_TIMEOUT
    while count > 0:
        time.sleep(1)
        count -= 1
    print('Finished cleanup...')

def checkpoint():
    pass

def main():
    """Opening subprocesses"""
    global stream
    port = '30173'
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    mpi_monitor_pb2_grpc.add_MonitorServicer_to_server(Monitor(), server)
    server.add_insecure_port('[::]:' + port)
    server.start()
    print("Server started, listening on " + port)

    stream = subprocess.Popen(["/home/hpc-tests/stream/stream_c"] + sys.argv[1:], preexec_fn=os.setsid)
    
    while stream.poll() is None:
        time.sleep(10)

    print("Finish execution...")

if __name__ == "__main__":
    main()
