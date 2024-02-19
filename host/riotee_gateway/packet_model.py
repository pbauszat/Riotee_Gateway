from pydantic import BaseModel
from pydantic import validator
from pydantic import Field
from datetime import datetime
import numpy as np
import base64


class PacketBase(BaseModel):
    data: bytes
    pkt_id: int

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

    @validator("pkt_id", check_fields=False)
    def pkt_id_is_uint16(cls, val):
        if val < 0 or val >= 2**16:
            raise ValueError("outside range for uint16")
        else:
            return val

    @validator("ack_id", check_fields=False)
    def ack_id_is_uint16(cls, val):
        if val is None:
            return val
        if val < 0 or val >= 2**16:
            raise ValueError("outside range for uint16")
        else:
            return val


class PacketApiSend(PacketBase):
    """Packet sent to the Gateway server via API to be forwarded to a device."""

    @classmethod
    def from_binary(cls, data: bytes, pkt_id: np.int16 = None):
        data_enc = base64.urlsafe_b64encode(data)
        return cls(data=data_enc, pkt_id=pkt_id)


class PacketTransceiverSend(PacketBase):
    """Packet sent to the transceiver via USB CDC ACM."""

    dev_id: bytes

    @classmethod
    def from_PacketApiSend(cls, pkt: PacketApiSend, dev_id: bytes):
        return cls(pkt_id=pkt.pkt_id, data=pkt.data, dev_id=dev_id)

    def to_uart(self):
        """Returns a string ready to be sent to the gateway transceiver."""
        dev_id_enc = str(self.dev_id, "utf-8")
        data_enc = str(self.data, "utf-8")
        pkt_id_enc = str(base64.urlsafe_b64encode(np.uint16(self.pkt_id)), "utf-8")
        return bytes(f"[{dev_id_enc}\0{pkt_id_enc}\0{data_enc}\0]", encoding="utf-8")


class PacketApiReceive(PacketBase):
    """Packet received by the Gateway server from a device to be retrieved via the API."""

    dev_id: bytes
    pkt_id: int
    ack_id: int
    timestamp: datetime
    dongle_timestamp: int  # np.uint64

    @staticmethod
    def str_extract(pkt_str: bytes):
        """Extracts a null-terminated base64 string from pkt_str and converts it to utf-8."""
        term_idx = pkt_str.find(b"\0")
        if term_idx < 0:
            raise Exception("Could not find terminating character")
        return pkt_str[:term_idx], term_idx

    @staticmethod
    def base64_to_bytes(pkt_str: bytes):
        """Extracts a null-terminated base64 string from pkt_str and converts it to utf-8."""
        pkt_str_cut, term_idx = PacketApiReceive.str_extract(pkt_str)
        return base64.urlsafe_b64decode(pkt_str_cut), term_idx

    @staticmethod
    def base64_to_bin(pkt_str: bytes, dtype):
        """Extracts a null-terminated base64 string from pkt_str and converts it to specified type."""
        binbytes, term_idx = PacketApiReceive.base64_to_bytes(pkt_str)
        return np.frombuffer(binbytes, dtype)[0], term_idx

    @classmethod
    def from_uart(cls, pkt_str: str, timestamp: datetime):
        """Populates class from a pkt_str received from the gateway transceiver."""
        dev_id, term_idx = cls.str_extract(pkt_str)
        pkt_str = pkt_str[term_idx + 1 :]

        pkt_id, term_idx = cls.base64_to_bin(pkt_str, np.uint16)
        pkt_str = pkt_str[term_idx + 1 :]

        ack_id, term_idx = cls.base64_to_bin(pkt_str, np.uint16)
        pkt_str = pkt_str[term_idx + 1 :]

        dongle_timestamp, term_idx = cls.base64_to_bin(pkt_str, np.uint64)
        pkt_str = pkt_str[term_idx + 1 :]

        data, _ = cls.str_extract(pkt_str)

        return cls(dev_id=dev_id, pkt_id=pkt_id, ack_id=ack_id, data=data, timestamp=timestamp, dongle_timestamp=dongle_timestamp)

    @classmethod
    def from_json(cls, json_dict: dict):
        return cls(
            dev_id=json_dict["dev_id"],
            pkt_id=json_dict["pkt_id"],
            ack_id=json_dict["ack_id"],
            data=json_dict["data"],
            timestamp=json_dict["timestamp"],
        )

    def to_json(self):
        json_dict = {"ack_id": self.ack_id, "pkt_id": self.pkt_id}
        json_dict["dev_id"] = str(self.dev_id, encoding="utf-8")
        json_dict["data"] = str(self.data, encoding="utf-8")
        json_dict["timestamp"] = str(self.timestamp)
        return json_dict
