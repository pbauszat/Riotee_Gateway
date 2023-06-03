import requests
from packet_model import PacketApiSend
import numpy as np
import base64


def decode_dev_id(dev_id_b64: str):
    return np.frombuffer(base64.urlsafe_b64decode(dev_id_b64), dtype=np.uint16)[0]


def encode_data(data):
    return str(base64.urlsafe_b64encode(data), "utf-8")


class GatewayConnection(object):
    def __init__(self, host: str = "localhost", port: int = 8000):
        self.__url = f"http://{host}:{port}"

    def convert_dev_id(fn_called):
        """Automatically converts dev_id argument to base64"""

        def _convert_dev_id_wrapped(self, dev_id: int | str, *args):
            if type(dev_id) is str:
                return fn_called(self, dev_id, *args)
            dev_id_b64 = encode_data(np.uint32(dev_id))
            return fn_called(self, dev_id_b64, *args)

        return _convert_dev_id_wrapped

    def get_devices(self):
        """Reads the list of all devices from which the gateway has received packets."""
        r = requests.get(f"{self.__url}/devices/list")
        return r.json()

    @convert_dev_id
    def send_packet(self, dev_id: int | str, pkt: PacketApiSend):
        r = requests.post(f"{self.__url}/devices/{dev_id}/send", data=pkt.json())

    @convert_dev_id
    def send_ascii(self, dev_id: int | str, text: str):
        pkt = PacketApiSend(data=encode_data(bytes(text, encoding="utf-8")))
        self.send_packet(dev_id, pkt)

    @convert_dev_id
    def get_queue_size(self, dev_id: int | str):
        """Reads the number of packets in the queue for the corresponding device."""
        r = requests.get(f"{self.__url}/devices/{dev_id}/size")
        return r.json()

    @convert_dev_id
    def get_packet(self, dev_id: int | str):
        """Retrieves a packet from the gateways fifo queue."""
        r = requests.get(f"{self.__url}/devices/{dev_id}/pop")
        return r.json()

    @convert_dev_id
    def get_packets(self, dev_id: int | str = None):
        if dev_id is None:
            r = requests.get(f"{self.__url}/all")
        else:
            r = requests.get(f"{self.__url}/devices/{dev_id}/all")
        return r.json()
