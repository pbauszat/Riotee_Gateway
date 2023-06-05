from pydantic import BaseModel
from pydantic import validator
from pydantic import Field
from datetime import datetime
import numpy as np
import base64


class PacketBase(BaseModel):
    data: bytes
    pkt_id: int | None

    @validator("data", check_fields=False)
    def is_data_base64(cls, val):
        val_bytes = base64.urlsafe_b64decode(val)
        if len(val_bytes) > 247:
            raise ValueError("data too long")
        return val

    @validator("dev_id", check_fields=False)
    def is_device_id(cls, val):
        val_bytes = base64.urlsafe_b64decode(val)
        if len(val_bytes) != 4:
            raise ValueError("device id has wrong size")
        return val

    @validator("pkt_id", "ack_id", check_fields=False)
    def is_uint16(cls, val):
        if val is None:
            return val
        if val < 0 or val >= 2**16:
            raise ValueError("outside range for uint16")
        else:
            return val


class PacketApiSend(PacketBase):
    """Packet sent to the Gateway server via API to be forwarded to a device."""

    @classmethod
    def from_binary(cls, data: bytes, pkt_id: int = None):
        data_enc = base64.urlsafe_b64encode(data)
        return cls(data=data_enc, pkt_id=pkt_id)


class PacketTransceiverSend(PacketBase):
    """Packet sent to the transceiver via USB CDC ACM."""

    dev_id: bytes
    pkt_id: int

    @classmethod
    def from_PacketApiSend(cls, pkt: PacketApiSend, dev_id: bytes):
        # assign a random packet ID if none is specified
        if pkt.pkt_id is None:
            pkt_id = np.random.randint(0, 2**16)
        else:
            pkt_id = pkt.pkt_id

        return cls(pkt_id=pkt_id, data=pkt.data, dev_id=dev_id)

    def to_uart(self):
        """Returns a string ready to be sent to the gateway transceiver."""
        dev_id_bytes = base64.urlsafe_b64decode(self.dev_id)
        dev_id_enc = str(base64.b64encode(dev_id_bytes), "utf-8")
        pkt_id_enc = str(base64.b64encode(np.uint16(self.pkt_id)), "utf-8")
        data_bytes = base64.urlsafe_b64decode(self.data)
        data_enc = str(base64.b64encode(data_bytes), "utf-8")
        return bytes(f"[{dev_id_enc}\0{pkt_id_enc}\0{data_enc}\0]", encoding="utf-8")


class PacketApiReceive(PacketBase):
    """Packet received by the Gateway server from a device to be retrieved via the API."""

    dev_id: bytes
    pkt_id: int
    ack_id: int
    timestamp: datetime

    @staticmethod
    def base64_to_bytes(pkt_str: bytes):
        """Extracts a null-terminated base64 string from pkt_str and converts it to utf-8."""
        term_idx = pkt_str.find(b"\0")
        if term_idx < 0:
            raise Exception("Could not find terminating character")
        return base64.b64decode(pkt_str[:term_idx], validate=True), term_idx

    @staticmethod
    def base64_to_bin(pkt_str: bytes, dtype):
        """Extracts a null-terminated base64 string from pkt_str and converts it to specified type."""
        binbytes, term_idx = PacketApiReceive.base64_to_bytes(pkt_str)
        return np.frombuffer(binbytes, dtype)[0], term_idx

    @classmethod
    def from_uart(cls, pkt_str: str, timestamp: datetime):
        """Populates class from a pkt_str received from the gateway transceiver."""
        # Re-encode as URL safe base64
        dev_id, term_idx = cls.base64_to_bytes(pkt_str)
        dev_id = base64.urlsafe_b64encode(dev_id)
        pkt_str = pkt_str[term_idx + 1 :]

        pkt_id, term_idx = cls.base64_to_bin(pkt_str, np.uint16)
        pkt_str = pkt_str[term_idx + 1 :]

        ack_id, term_idx = cls.base64_to_bin(pkt_str, np.uint16)

        pkt_str = pkt_str[term_idx + 1 :]

        # Re-encode as URL safe base64
        data, _ = cls.base64_to_bytes(pkt_str)
        data = base64.urlsafe_b64encode(data)

        return cls(dev_id=dev_id, pkt_id=pkt_id, ack_id=ack_id, data=data, timestamp=timestamp)
