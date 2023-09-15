from concurrent import futures
import logging

import grpc
import mpi_monitor_pb2
import mpi_monitor_pb2_grpc

class Monitor(mpi_monitor_pb2_grpc.MonitorServicer):

    def SendResources(self, request, context):
        print("We received the message SendResources")
        if(request.nodes == 4):
            print("Four nodes!!")
        return mpi_monitor_pb2.Confirmation(confirmMessage='Hello!')
    def activeNode(self, request, context):
        return mpi_monitor_pb2.Confirmation(confirmMessage='Hello again!')

def serve():
    port = '50051'
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    mpi_monitor_pb2_grpc.add_MonitorServicer_to_server(Monitor(), server)
    server.add_insecure_port('[::]:' + port)
    server.start()
    print("Server started, listening on " + port)
    server.wait_for_termination()


if __name__ == '__main__':
    logging.basicConfig()
    serve()
