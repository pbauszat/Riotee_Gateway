from pydantic import BaseModel
from datetime import datetime
import numpy as np
import base64


class Item(BaseModel):
    myval: int


class Packet(BaseModel):
    pkt_id: str
    dev_id: str
    ack_id: str | None
    data: str | None
    timestamp: datetime | None

    @staticmethod
    def base64_to_ascii(pkt_str: bytes):
        """Extracts a null-terminated base64 string from pkt_str and converts it to utf-8."""
        term_idx = pkt_str.find(b"\0")
        if term_idx < 0:
            raise Exception("Could not find terminating character")
        return str(pkt_str[:term_idx], "utf-8"), term_idx

    @classmethod
    def from_uart(cls, pkt_str: str):
        """Populates class from a pkt_str received from the gateway transceiver."""
        dev_id, term_idx = cls.base64_to_ascii(pkt_str)
        pkt_str = pkt_str[term_idx + 1 :]

        pkt_id, term_idx = cls.base64_to_ascii(pkt_str)
        pkt_str = pkt_str[term_idx + 1 :]

        ack_id, term_idx = cls.base64_to_ascii(pkt_str)
        pkt_str = pkt_str[term_idx + 1 :]

        data, term_idx = cls.base64_to_ascii(pkt_str)

        return cls(
            dev_id=dev_id,
            pkt_id=pkt_id,
            ack_id=ack_id,
            data=data,
        )

    @classmethod
    def from_binary(cls, dev_id: int, pkt_id: int, data: bytes = None):
        """Populates class from binary values."""
        dev_id_enc = str(base64.urlsafe_b64encode(np.uint32(dev_id)), "utf-8")
        pkt_id_enc = str(base64.urlsafe_b64encode(np.uint16(pkt_id)), "utf-8")
        if data is not None:
            data_enc = str(base64.urlsafe_b64encode(data), "utf-8")
        else:
            data_enc = None

        return cls(dev_id=dev_id_enc, pkt_id=pkt_id_enc, data=data_enc)

    def to_uart(self):
        if self.ack_id is None:
            ack_id_str = str(base64.urlsafe_b64encode(np.uint16(0)), "utf-8")
        else:
            ack_id_str = self.ack_id

        data_str = self.data if self.data is not None else ""

        """Returns a string ready to be sent to the gateway transceiver."""
        return bytes(f"[{self.dev_id}\0{self.pkt_id}\0{ack_id_str}\0{data_str}\0]", encoding="utf-8")
