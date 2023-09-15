from __future__ import print_function

import logging

import grpc
import mpi_monitor_pb2
import mpi_monitor_pb2_grpc


def run():
    # NOTE(gRPC Python Team): .close() is possible on a channel and should be
    # used in circumstances in which the with statement does not fit the needs
    # of the code.
    print("Will try to send message ...")
    with grpc.insecure_channel('localhost:50051') as channel:
        stub = mpi_monitor_pb2_grpc.MonitorStub(channel)
        response = stub.SendResources(mpi_monitor_pb2.availNodes(nodes=4))
        print("Greeter client received: " + response.confirmMessage)
        response = stub.activeNode(mpi_monitor_pb2.nodeName(nodeName='mpimaster-k8s-0'))
        print("Greeter client received: " + response.confirmMessage)

if __name__ == '__main__':
    logging.basicConfig()
    run()
