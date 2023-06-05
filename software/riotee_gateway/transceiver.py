import serial_asyncio
from serial.tools import list_ports
from datetime import datetime
import logging

from riotee_gateway.packet_model import PacketApiReceive
from riotee_gateway.packet_model import PacketTransceiverSend


class Transceiver(object):
    """Represents the nRF board that communicates with the devices wirelessly."""

    USB_PID = 0xC8A2
    USB_VID = 0x1209

    @staticmethod
    def find_serial_port() -> str:
        """Finds the serial port name of an attached gateway dongle based on USB IDs."""
        hits = list()
        for port in list_ports.comports():
            if port.vid == Transceiver.USB_VID and port.pid == Transceiver.USB_PID:
                hits.append(port.device)

        if not hits:
            raise Exception("Couldn't find serial port of Riotee Gateway.")
        elif len(hits) == 1:
            logging.info(f"Found serial port at {hits[0]}")
            return hits[0]
        else:
            raise Exception(f"Found multiple potential devices at {' and '.join(hits)}")

    def __init__(self, port: str = None, baudrate: int = 1000000):
        self.__port = port
        self.__baudrate = baudrate

    async def __enter__(self):
        if self.__port is None:
            self.__port = Transceiver.find_serial_port()

        self.__reader, self.__writer = await serial_asyncio.open_serial_connection(
            url=self.__port, baudrate=self.__baudrate
        )
        return self

    def __exit__(self, *args):
        pass

    async def read_packet(self):
        await self.__reader.readuntil(b"[")
        pkt_str = await self.__reader.readuntil(b"]")
        return PacketApiReceive.from_uart(pkt_str, datetime.now())

    def send_packet(self, pkt: PacketTransceiverSend):
        self.__writer.write(pkt.to_uart())
        logging.debug(pkt.to_uart())
