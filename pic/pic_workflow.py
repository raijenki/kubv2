from airflow import DAG
from airflow.models.param import Param
from airflow.decorators import dag, task, task_group
from airflow.configuration import conf
from airflow.providers.cncf.kubernetes.operators.kubernetes_pod import KubernetesPodOperator
from airflow.utils.task_group import TaskGroup
from datetime import datetime

from kubernetes.client import models as k8s

PVC_NAME = 'pvc-autodock'
MOUNT_PATH = '/data'
VOLUME_KEY  = 'volume-autodock'
namespace = conf.get('kubernetes_executor', 'NAMESPACE')
pvol_path = "/home/daniel/k3dvol/gem/"

def create_pod_spec(pic_id, wtype):
    volume = k8s.V1Volume(
        name=VOLUME_KEY,
        persistent_volume_claim=k8s.V1PersistentVolumeClaimVolumeSource(claim_name=PVC_NAME),
    )
    volume_mount = k8s.V1VolumeMount(mount_path=MOUNT_PATH, name=VOLUME_KEY)
    pod_name = "pic-" +  str(wtype) + "-" + str(pic_id)

    # define a generic container, which can be used for all tasks
    container = k8s.V1Container(
        name=pod_name,
        image='raijenki/mpik8s:picv50',
        working_dir=MOUNT_PATH,

        volume_mounts=[volume_mount],
        image_pull_policy='Always',
    )

    pod_metadata = k8s.V1ObjectMeta(name=pod_name)
    # CPU/generic pod specification
    pod_spec      = k8s.V1PodSpec(containers=[container], volumes=[volume])
    full_pod_spec = k8s.V1Pod(metadata=pod_metadata, spec=pod_spec)
    return full_pod_spec

params = {
        'ninputs': 3,
}

@task
def list_inputs(params=None):
    return [f'pic-worker-{i}' for i in range(0, int(params['ninputs']))]

@dag(start_date=datetime(2021, 1, 1),
     schedule=None,
     catchup=False,
     params=params)
def pic(): 
    import os.path
    
    end_exec = KubernetesPodOperator(
        task_id='end_exec',
        full_pod_spec=create_pod_spec(0, 'end'),
        cmds = ['sleep 10'],
    )

    tracker = KubernetesPodOperator(
            task_id='tracker',
            full_pod_spec=create_pod_spec(0, 'tracker'),
            cmds=['python3', '/home/tracker.py'],
            get_logs=True,
         )


    prepare_inputs = KubernetesPodOperator(
            task_id='prepare_inputs',
            full_pod_spec=create_pod_spec(0, 'prepare-inputs'),
            cmds=['python3'],
            arguments=['/home/preparation.py'],
        )
    
    picexec = KubernetesPodOperator.partial(
        task_id=f'pic-worker',
        full_pod_spec=create_pod_spec(0, 'worker'),
        cmds = ['/bin/sh', '-c']
        arguments=['/home/exec_pic.sh'],
    ).expand(name=list_inputs())
   
    # prepare_inputs >> picexec
    # prepare_inputs >> tracker


    #d = exec_pic.expand(batch_label=ninputs_array)
    
    prepare_inputs >> [picexec, tracker] >> end_exec


pic()
