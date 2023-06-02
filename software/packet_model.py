from pydantic import BaseModel


class Packet(BaseModel):
    pkt_id: str
    dev_id: str
    acknowledgement_id: str | None
    data: str | None

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

        acknowledgement_id, term_idx = cls.base64_to_ascii(pkt_str)
        pkt_str = pkt_str[term_idx + 1 :]

        data, term_idx = cls.base64_to_ascii(pkt_str)

        return cls(
            dev_id=dev_id,
            pkt_id=pkt_id,
            acknowledgement_id=acknowledgement_id,
            data=data,
        )

    def to_uart(self):
        """Returns a string ready to be sent to the gateway transceiver."""
        return (
            f"[{self.dev_id}\0{self.pkt_id}\0{self.acknowledgement_id}\0{self.data}\0]"
        )
