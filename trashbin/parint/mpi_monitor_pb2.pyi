from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional

DESCRIPTOR: _descriptor.FileDescriptor

class Confirmation(_message.Message):
    __slots__ = ["confirmId", "confirmMessage"]
    CONFIRMID_FIELD_NUMBER: _ClassVar[int]
    CONFIRMMESSAGE_FIELD_NUMBER: _ClassVar[int]
    confirmId: int
    confirmMessage: str
    def __init__(self, confirmMessage: _Optional[str] = ..., confirmId: _Optional[int] = ...) -> None: ...

class Dummy22(_message.Message):
    __slots__ = ["mtest"]
    MTEST_FIELD_NUMBER: _ClassVar[int]
    mtest: str
    def __init__(self, mtest: _Optional[str] = ...) -> None: ...

class SSHKeys(_message.Message):
    __slots__ = ["confirmId", "privJobKey", "pubJobKey"]
    CONFIRMID_FIELD_NUMBER: _ClassVar[int]
    PRIVJOBKEY_FIELD_NUMBER: _ClassVar[int]
    PUBJOBKEY_FIELD_NUMBER: _ClassVar[int]
    confirmId: int
    privJobKey: str
    pubJobKey: str
    def __init__(self, pubJobKey: _Optional[str] = ..., privJobKey: _Optional[str] = ..., confirmId: _Optional[int] = ...) -> None: ...

class additionalNodes(_message.Message):
    __slots__ = ["mode", "nodes"]
    MODE_FIELD_NUMBER: _ClassVar[int]
    NODES_FIELD_NUMBER: _ClassVar[int]
    mode: str
    nodes: int
    def __init__(self, nodes: _Optional[int] = ..., mode: _Optional[str] = ...) -> None: ...

class nodeName(_message.Message):
    __slots__ = ["nodeIP"]
    NODEIP_FIELD_NUMBER: _ClassVar[int]
    nodeIP: str
    def __init__(self, nodeIP: _Optional[str] = ...) -> None: ...
