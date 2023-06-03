from serial.tools import list_ports
from serial import Serial
import asyncio
from fastapi import FastAPI
import uvicorn
from packet_model import *
import logging
from queue import Queue
import click
from datetime import datetime
import serial_asyncio


class PacketDatabase(object):
    """Stores received packets until they are retrieved over the API."""

    def __init__(self) -> None:
        self.__db = dict()

    def add_packet(self, pkt: PacketApiReceive):
        try:
            self.__db[pkt.dev_id].put(pkt)
        except KeyError:
            self.__db[pkt.dev_id] = Queue(1024)
            self.__db[pkt.dev_id].put_nowait(pkt)

    def get_packet(self, dev_id):
        return self.__db[dev_id].get_nowait()

    def get_packets(self, dev_id):
        packets = list()
        while self.get_queue_size() > 0:
            packets.append(self.get_packet())
        return packets

    def get_devices(self):
        return list(self.__db.keys())

    def get_queue_size(self, dev_id):
        return self.__db[dev_id].qsize()


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

        self.__reader, self.__writer = await serial_asyncio.open_serial_connection(url=self.__port, baudrate=self.__baudrate)
        return self

    def __exit__(self, *args):
        pass

    async def read_packet(self):
        await self.__reader.readuntil(b"[")
        pkt_str = await self.__reader.readuntil(b"]")
        return pkt_str

    def send_packet(self, pkt: PacketTransceiver):
        self.__writer.write(pkt.to_uart())
        logging.debug(pkt.to_uart())

    async def run(self, db):
        while True:
            pkt_str = await tcv.read_packet()
            pkt = PacketApiReceive.from_uart(pkt_str, datetime.now())
            db.add_packet(pkt)
            logging.debug(f"Got packet from {pkt.dev_id} with ID {pkt.pkt_id} @{pkt.timestamp}")


tcv = Transceiver()
db = PacketDatabase()
app = FastAPI()


@app.get("/")
async def get_root():
    return "Welcome to the Gateway!"


@app.get("/devices/list")
async def get_devices():
    return db.get_devices()


@app.get("/devices/all")
async def get_devices():
    packets = list()
    for dev_id in db.get_devices():
        packets += db.get_packets(dev_id)


@app.get("/devices/{device_id}/size")
async def get_queue_size(device_id: str):
    return db.get_queue_size(device_id)


@app.get("/devices/{device_id}/pop")
async def get_packet(device_id: str):
    print(device_id)
    return db.get_packet(device_id)


@app.get("/devices/{device_id}/all")
async def get_packet(device_id: str):
    return db.get_packets(device_id)


@app.post("/devices/{device_id}/send")
async def post_packet(device_id: str, packet: PacketApiSend):
    print("T", device_id)
    pkt_tcv = PacketTransceiver.from_PacketApiSend(packet, device_id)
    tcv.send_packet(pkt_tcv)


@app.on_event("startup")
async def startup_event():
    await tcv.__enter__()
    asyncio.create_task(tcv.run(db))


@app.on_event("shutdown")
def shutdown_event():
    tcv.__exit__()


@click.option("-v", "--verbose", count=True, default=2)
@click.option(
    "-d",
    "--device",
    type=str,
    help="Serial device of gateway transceiver",
)
@click.option("-p", "--port", type=int, default=8000, help="Port for API server")
@click.option("-h", "--host", type=str, default="0.0.0.0", help="Host for API server")
@click.command
def cli(host, port, device, verbose):
    if verbose == 0:
        logging.basicConfig(level=logging.ERROR)
    elif verbose == 1:
        logging.basicConfig(level=logging.WARNING)
    elif verbose == 2:
        logging.basicConfig(level=logging.INFO)
    elif verbose > 2:
        logging.basicConfig(level=logging.DEBUG)

    uvicorn.run("server:app", port=port, host=host)


if __name__ == "__main__":
    cli()
