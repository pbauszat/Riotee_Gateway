from pydantic import BaseModel
from pydantic import validator
from datetime import datetime
import numpy as np
import base64


class PacketBase(BaseModel):
    data: str
    pkt_id: str | None

    @validator("dev_id", "pkt_id", "data", "ack_id", check_fields=False)
    def is_base64(cls, val):
        if val is None:
            return val
        if val == "":
            raise ValueError("cannot be empty")

        base64.urlsafe_b64decode(val)
        return val


class PacketApiSend(PacketBase):
    """Packet sent to the Gateway server via API to be forwarded to a device."""

    @classmethod
    def from_binary(cls, data: bytes, pkt_id: int = None):
        """Populates class from binary values."""
        if pkt_id is not None:
            pkt_id_enc = str(base64.urlsafe_b64encode(np.uint16(pkt_id)), "utf-8")
        else:
            pkt_id_enc = None

        data_enc = str(base64.urlsafe_b64encode(data), "utf-8")

        return cls(pkt_id=pkt_id_enc, data=data_enc)


class PacketTransceiver(PacketBase):
    """Packet sent to the transceiver via USB CDC ACM."""

    pkt_id: str
    dev_id: str

    @classmethod
    def from_PacketApiSend(cls, pkt: PacketApiSend, dev_id):
        # assign a random packet ID if none is specified
        if pkt.pkt_id is None:
            pkt_id = str(base64.urlsafe_b64encode(np.uint16(np.random.randint(0, 2**16))), "utf-8")
        else:
            pkt_id = pkt.pkt_id
        return cls(pkt_id=pkt_id, data=pkt.data, dev_id=dev_id)

    def to_uart(self):
        """Returns a string ready to be sent to the gateway transceiver."""
        return bytes(f"[{self.dev_id}\0{self.pkt_id}\0{self.data}\0]", encoding="utf-8")


class PacketApiReceive(PacketBase):
    """Packet received by the Gateway server from a device to be retrieved via the API."""

    ack_id: str
    timestamp: datetime
    pkt_id: str
    dev_id: str

    @staticmethod
    def base64_to_ascii(pkt_str: bytes):
        """Extracts a null-terminated base64 string from pkt_str and converts it to utf-8."""
        term_idx = pkt_str.find(b"\0")
        if term_idx < 0:
            raise Exception("Could not find terminating character")
        return str(pkt_str[:term_idx], "utf-8"), term_idx

    @classmethod
    def from_uart(cls, pkt_str: str, timestamp: datetime):
        """Populates class from a pkt_str received from the gateway transceiver."""
        dev_id, term_idx = cls.base64_to_ascii(pkt_str)
        pkt_str = pkt_str[term_idx + 1 :]

        pkt_id, term_idx = cls.base64_to_ascii(pkt_str)
        pkt_str = pkt_str[term_idx + 1 :]

        ack_id, term_idx = cls.base64_to_ascii(pkt_str)
        pkt_str = pkt_str[term_idx + 1 :]

        data, _ = cls.base64_to_ascii(pkt_str)

        return cls(dev_id=dev_id, pkt_id=pkt_id, ack_id=ack_id, data=data, timestamp=timestamp)
