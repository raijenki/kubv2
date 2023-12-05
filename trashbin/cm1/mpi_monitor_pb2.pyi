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

class Empty(_message.Message):
    __slots__ = []
    def __init__(self) -> None: ...

class SSHKeys(_message.Message):
    __slots__ = ["confirmId", "privJobKey", "pubJobKey"]
    CONFIRMID_FIELD_NUMBER: _ClassVar[int]
    PRIVJOBKEY_FIELD_NUMBER: _ClassVar[int]
    PUBJOBKEY_FIELD_NUMBER: _ClassVar[int]
    confirmId: int
    privJobKey: str
    pubJobKey: str
    def __init__(self, pubJobKey: _Optional[str] = ..., privJobKey: _Optional[str] = ..., confirmId: _Optional[int] = ...) -> None: ...

class Scale(_message.Message):
    __slots__ = ["nodes"]
    NODES_FIELD_NUMBER: _ClassVar[int]
    nodes: int
    def __init__(self, nodes: _Optional[int] = ...) -> None: ...

class additionalNodes(_message.Message):
    __slots__ = ["nodes"]
    NODES_FIELD_NUMBER: _ClassVar[int]
    nodes: int
    def __init__(self, nodes: _Optional[int] = ...) -> None: ...

class nodeName(_message.Message):
    __slots__ = ["nodeName"]
    NODENAME_FIELD_NUMBER: _ClassVar[int]
    nodeName: str
    def __init__(self, nodeName: _Optional[str] = ...) -> None: ...
