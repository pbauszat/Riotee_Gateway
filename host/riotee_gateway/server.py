import asyncio
from fastapi import FastAPI
from fastapi import HTTPException
import logging

from riotee_gateway.packet_model import *
from riotee_gateway.transceiver import Transceiver


class PacketDatabase(object):
    """Stores received packets until they are retrieved over the API."""

    def __init__(self) -> None:
        self.__db = dict()

    def add(self, pkt: PacketApiReceive):
        try:
            self.__db[pkt.dev_id].append(pkt)
        except KeyError:
            self.__db[pkt.dev_id] = [pkt]

    def reset(self, dev_id):
        self.__db[dev_id] = list()

    def get_devices(self):
        return list(self.__db.keys())

    def __getitem__(self, dev_id):
        return self.__db[dev_id]


async def receive_loop(tcv: Transceiver, db: PacketDatabase):
    while True:
        pkt = await tcv.read_packet()
        db.add(pkt)
        logging.debug(f"Got packet from {pkt.dev_id} with ID {pkt.pkt_id} @{pkt.timestamp}")


tcv: Transceiver = None
db = PacketDatabase()
app = FastAPI()


@app.get("/")
async def get_root():
    return "Welcome to the Riotee Gateway!"


@app.get("/devices")
async def get_devices():
    return db.get_devices()


@app.get("/in/all/size")
async def get_all_queue_size():
    n_tot = 0
    for dev_id in db.get_devices():
        n_tot += len(db[dev_id])
    return n_tot


@app.get("/in/all/all")
async def get_all_packets():
    pkts = list()
    for dev_id in db.get_devices():
        pkts += db[dev_id]
    return pkts


@app.delete("/in/all/all")
async def delete_all_packets():
    pkts = list()
    for dev_id in db.get_devices():
        db.reset(dev_id)


@app.get("/in/{dev_id}/size")
async def get_queue_size(dev_id: bytes):
    try:
        return len(db[dev_id])
    except KeyError:
        raise HTTPException(status_code=404, detail="Device not found")


@app.get("/in/{dev_id}/all")
async def get_all_dev_packets(dev_id: bytes):
    try:
        return db[dev_id]
    except KeyError:
        raise HTTPException(status_code=404, detail="Device not found")


@app.delete("/in/{dev_id}/all")
async def delete_all_devpackets(dev_id: bytes):
    try:
        db.reset(dev_id)
    except KeyError:
        raise HTTPException(status_code=404, detail="Device not found")


@app.get("/in/{dev_id}/{index}")
async def get_packet(dev_id: bytes, index: int):
    try:
        return db[dev_id][index]
    except KeyError:
        raise HTTPException(status_code=404, detail="Device not found")
    except IndexError:
        raise HTTPException(status_code=404, detail="Packet not found")


@app.delete("/in/{dev_id}/{index}")
async def delete_packet(dev_id: bytes, index: int):
    try:
        del db[dev_id][index]
    except KeyError:
        raise HTTPException(status_code=404, detail="Item not found")
    except IndexError:
        raise HTTPException(status_code=404, detail="Packet not found")


@app.post("/out/{dev_id}")
async def post_packet(dev_id: bytes, packet: PacketApiSend):
    pkt_tcv = PacketTransceiverSend.from_PacketApiSend(packet, dev_id)
    tcv.send_packet(pkt_tcv)
    return packet


@app.on_event("startup")
async def startup_event():
    await tcv.__aenter__()
    asyncio.create_task(receive_loop(tcv, db))


@app.on_event("shutdown")
def shutdown_event():
    tcv.__exit__()
