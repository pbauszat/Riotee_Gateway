from serial.tools import list_ports
from serial import Serial
import asyncio
from fastapi import FastAPI
import uvicorn
from packet_model import Packet
import logging
from queue import Queue
import click
from datetime import datetime


class PacketDatabase(object):
    """Stores received packets until they are retrieved over the API."""

    def __init__(self) -> None:
        self.__db = dict()

    def add_packet(self, pkt: Packet):
        try:
            self.__db[pkt.dev_id].put(pkt)
        except KeyError:
            self.__db[pkt.dev_id] = Queue(1024)
            self.__db[pkt.dev_id].put(pkt)

    def get_packet(self, dev_id):
        return self.__db[dev_id].get()

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

    def __init__(self, port=None) -> None:
        self.__port = port
        self.__ser = None

    def __enter__(self):
        if self.__port is None:
            self.__port = Transceiver.find_serial_port()

        self.__ser = Serial(self.__port, 1000000, timeout=0)
        return self

    def __exit__(self, *args):
        self.__ser.close()

    def send_packet(self, pkt: Packet):
        self.__ser.write(pkt.to_uart())

    async def read_packet(self, pkt_buf=bytes()):
        """Searches the bytestring pkt_buf for a valid packet descriptor."""
        last_idx = 0
        while True:
            pkt_snippet_start_idx = pkt_buf[last_idx:].find(b"[")
            if pkt_snippet_start_idx >= 0:
                break
            # We don't need to search the part up to last_idx again
            last_idx = len(pkt_buf)
            # Try to get more data from the transceiver
            while True:
                pkt_buf += self.__ser.read_all()
                if len(pkt_buf) > last_idx:
                    break
                await asyncio.sleep(0.001)

        pkt_start_idx = last_idx + pkt_snippet_start_idx

        last_idx = 0
        pkt_buf = pkt_buf[pkt_start_idx + 1 :]
        while True:
            pkt_snippet_end_idx = pkt_buf[last_idx:].find(b"]")
            if pkt_snippet_end_idx >= 0:
                break
            last_idx = len(pkt_buf)
            while True:
                pkt_buf += self.__ser.read_all()
                if len(pkt_buf) > last_idx:
                    break
                await asyncio.sleep(0.001)

        pkt_end_idx = last_idx + pkt_snippet_end_idx
        # Return the discovered descriptor and the remaining portion of the bytestring
        return pkt_buf[:pkt_end_idx], pkt_buf[pkt_end_idx + 1 :]


async def transceiver_loop():
    remaining_buf = bytes()
    with Transceiver() as tcv:
        while True:
            pkt_buf, remaining_buf = await tcv.read_packet(remaining_buf)
            pkt = Packet.from_uart(pkt_buf)
            pkt.timestamp = datetime.now()
            db.add_packet(pkt)
            logging.debug(f"Got packet from {pkt.dev_id} with ID {pkt.pkt_id}")


db = PacketDatabase()
app = FastAPI()


@app.get("/")
async def get_root():
    return "Welcome to the Gateway!"


@app.get("/devices")
async def get_devices():
    return db.get_devices()


@app.get("/devices/{device_id}/size")
async def get_queue_size(device_id: str):
    return db.get_queue_size(device_id)


@app.get("/devices/{device_id}")
async def get_packet(device_id: str):
    return db.get_packet(device_id)


@app.post("/out")
async def post_packet(packet: Packet):
    db.add_packet(packet)
    return packet


@app.on_event("startup")
async def startup_event():
    asyncio.create_task(transceiver_loop())


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
