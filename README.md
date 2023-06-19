# Riotee Gateway

[![Python package build](https://github.com/NessieCircuits/Riotee_Gateway/actions/workflows/build-host.yml/badge.svg)](https://github.com/NessieCircuits/Riotee_Gateway/actions/workflows/build-host.yml)
[![Firmware build](https://github.com/NessieCircuits/Riotee_Gateway/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/NessieCircuits/Riotee_Gateway/actions/workflows/build-firmware.yml)

The Riotee Gateway communicates with Riotee devices via a proprietary wireless protocol.
The gateway forwards messages received from the devices to a client application and forwards messages from the client to the devices.
The gateway consists of three components:

The firmware runs on the [nRF52840-Dongle](https://www.nordicsemi.com/Products/Development-hardware/nrf52840-dongle). It listens for incoming messages from the Riotee devices using the built-in 2.4GHz radio.
A ZephyrOS-based firmware running on the Dongle dumps received messages to its USB port.
It also accepts messages sent over USB and stores them in an internal buffer until they are sent to the corresponding Riotee device.

A Python application running on a server communicates with the Dongle via USB.
It stores messages received from the dongle in an internal database.
The application exposes a REST API.

A client application running on a host (can be the same machine as the server) communicates with the server over HTTP.
The client remotely fetches messages received by the gateway and sends messages to the gateway using the API.

# Installation

To setup a Riotee Gateway, you need an nRF52840-Dongle and a computer with a USB port running **Python 3.10** or later.

First, the firmware must be flashed on the Dongle.
You can build the firmware yourself or download the [latest release](https://www.riotee.nessie-circuits.de/artifacts/gateway/latest/zephyr.hex).
To flash the hex file follow the instructions under "Program application using nRF Connect Programmer" on [this page](https://devzone.nordicsemi.com/guides/short-range-guides/b/getting-started/posts/nrf52840-dongle-programming-tutorial).

Next, install the Python application using your favorite package manager.
For pip:
```
pip install riotee-gateway
```

If you want to access the gateway remotely via the REST API, install the same Python package on the client device.

After installation, you can list the available commands for the Riotee gateway with:

```
riotee-gateway --help
```

# Usage

## Server
Attach the nRF52840-Dongle to the server's USB port.
On Linux, it may be necessary to install the udev rules provided in this repository to access the USB device with user privileges.
To start the server run
```
riotee-gateway server
```

The server should start listening on all interfaces and the default port 8000.
You can use the provided `riotee-gateway.service` as a starting point for setting up a permanent server.

## Client

The provided commandline interface offers an easy way to interact with the gateway:

On the client machine, run the following command to list all devices from which the gateway has received messages since the start:
```
riotee-gateway client --host [SERVER] devices
```
Replace [SERVER] with the hostname or IP of the server. If it is the same machine, you can omit the `--host` parameter.

To fetch all packets received from a specific device ID run
```
riotee-gateway client fetch -d [DEVICE_ID]
```
To store the received packets in a file, add the `-o received.txt` option.

To continuously poll the server for all packets received from any device and store them in a file `received.txt` run
```
riotee-gateway client monitor -o received.txt
```

For more advanced use cases, the client may also be used programatically by importing the corresponding class:

```python
from riotee_gateway import GatewayClient
gc = GatewayClient(host=localhost, port=8000)

for dev_id in gc.get_devices()
    pkts = gc.get_packets(dev_id)
    gc.delete_packets(dev_id)
```

# Data Format

The data received from the gateway is json-formatted.
The device address and the payload data are url-safe base64 encoded.
The python package provides two convenience methods for decoding the corresponding data fields:

```python
from riotee_gateway import base64_to_numpy, base64_to_devid

data_arr = base64_to_numpy(pkt.data, np.uint32)
dev_id = base64_to_devid(pkt.dev_id)
```
