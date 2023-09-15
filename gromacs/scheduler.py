import logging
import uuid
import grpc
import time
import mpi_monitor_pb2
import mpi_monitor_pb2_grpc
import copy
import sys
from kubernetes import client
from kubernetes import config, watch

total_clients = 0

logging.basicConfig(level=logging.INFO)
config.load_incluster_config()

# Use this one for local testing
#config.load_kube_config()

PVC_NAME = 'task-pv-claim'
MOUNT_PATH = '/data'
VOLUME_KEY  = 'task-pv-storage'

class Kubernetes:
    def __init__(self):

        # Init Kubernetes
        self.core_api = client.CoreV1Api()
        self.batch_api = client.BatchV1Api()

    @staticmethod
    def create_container(image, name, pull_policy):
        volume_mount = client.V1VolumeMount(mount_path=MOUNT_PATH, name=VOLUME_KEY)

        container = client.V1Container(
            image=image,
            name=name,
            image_pull_policy=pull_policy,
            volume_mounts=[volume_mount],
            #args=["sleep", "5"]
            command=["/usr/bin/python3", "hpc-tests/gromacs/launcher.py"],
        )

        logging.info(
            f"Created container with name: {container.name}, "
            f"image: {container.image} and args: {container.args}"
        )

        return container

    @staticmethod
    def create_pod_template(pod_name, container):
        volume = client.V1Volume(
            name=VOLUME_KEY,
            persistent_volume_claim=client.V1PersistentVolumeClaimVolumeSource(claim_name=PVC_NAME),
        )
        pod_template = client.V1PodTemplateSpec(
            spec=client.V1PodSpec(restart_policy="Never", containers=[container], volumes=[volume]),
            metadata=client.V1ObjectMeta(name=pod_name, labels={"pod_name": pod_name}),
        )

        return pod_template

    @staticmethod
    def create_job(job_name, pod_template, num_pods):
        metadata = client.V1ObjectMeta(name=job_name, labels={"job_name": job_name})

        job = client.V1Job(
            api_version="batch/v1",
            kind="Job",
            metadata=metadata,
            spec=client.V1JobSpec(backoff_limit=0, template=pod_template, parallelism=num_pods, completions=num_pods),
        )

        return job

def create_additional_pods(num_pods, _job_name):
    pod_id = 0

    # Kubernetes instance
    k8s = Kubernetes()

    # STEP1: CREATE A CONTAINER
    #_image = "busybox"
    _image = "raijenki/mpik8s:gromacs"
    _name = "scheduler"
    _pull_policy = "Always"

    shuffler_container = k8s.create_container(_image, _name, _pull_policy)

    # STEP2: CREATE A POD TEMPLATE SPEC
    _pod_name = f"gmx-job-scale-{pod_id}" 
    _pod_spec = k8s.create_pod_template(_pod_name, shuffler_container)

    # STEP3: CREATE A JOB
    _job = k8s.create_job(_job_name, _pod_spec, num_pods)

    # STEP4: EXECUTE THE JOB
    batch_api = client.BatchV1Api()
    batch_api.create_namespaced_job("default", _job)
    print(f"Job created with {num_pods} pods running the container")

def monitor_job_completion(job_name):
    core_api = client.CoreV1Api()
    batch_api = client.BatchV1Api()

    # Watch for Job events
    w = watch.Watch()

    try:
        # Start watching Job events
        for event in w.stream(batch_api.list_namespaced_job, namespace="default"):
            job = event['object']

            # Check if the event is for the desired Job
            if job.metadata.name == job_name:
                # Check the Job status
                if job.status.succeeded == 1:
                    print("Job completed successfully.")

                    # Delete associated pods and job
                    pod_list = core_api.list_namespaced_pod(namespace="default", label_selector=f"job-name={job_name}")
                    batch_api.delete_namespaced_job(name=job_name, namespace="default")
                    for pod in pod_list.items:
                        core_api.delete_namespaced_pod(name=pod.metadata.name, namespace="default")

                    break

                elif job.status.failed == 1:
                    print("Job failed.")

                    # Delete the Job
                    pod_list = core_api.list_namespaced_pod(namespace="default", label_selector=f"job-name={job_name}")
                    batch_api.delete_namespaced_job(name=job_name, namespace="default")
                    for pod in pod_list.items:
                        core_api.delete_namespaced_pod(name=pod.metadata.name, namespace="default")

                    break

    except KeyboardInterrupt:
        # Stop watching if interrupted
        w.stop()
        print("Monitoring stopped.")


def scheduler(num_pods):
    job_id = uuid.uuid4()
    _job_name = f"gmx-job-scale-{job_id}"
    with grpc.insecure_channel('grpc-server.default:30173') as channel:
        stub = mpi_monitor_pb2_grpc.MonitorStub(channel)
        response = stub.Scale(mpi_monitor_pb2.additionalNodes(nodes=num_pods, mode='hpa'))
        print(response)
    create_additional_pods(int(num_pods), _job_name)
    monitor_job_completion(_job_name)
    return 0

if __name__ == "__main__":
    print(sys.argv[1])
    scheduler(int(sys.argv[1]))
