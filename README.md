# Riotee Gateway

The Riotee Gateway communicates with Riotee devices via a proprietary wireless protocol.
The gateway forwards messages received from the devices to a client application and forwards messages from the client to the devices.
The gateway consists of three components:

An nRF52840-Dongle listens for incoming messages from the Riotee devices using the built-in 2.4GHz radio.
A ZephyrOS-based firmware running on the Dongle dumps received messages to its USB port.
It also accepts messages sent over USB and stores them in an internal buffer until they are sent to the corresponding Riotee device.

A Python application running on a server communicates with the Dongle via USB.
It stores messages received from the dongle in an internal database.
The application exposes a REST API.

A Client application running on a host (can be the same as the server) communicates with the server over HTTP.
The client remotely fetches received messages and sends messages via the API.

# Installation

To setup a Riotee Gateway, you need an nRF52840-Dongle and a computer with a USB port.

First, the firmware must be flashed on the Dongle.
You can build the firmware yourself or download the [latest binary](todo).
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

Attach the nRF52840-Dongle to the server's USB port.
On Linux, it may be necessary to install the udev rules provided in this repository to access the USB device with user privileges.
To start the server run `riotee-gateway server`.
This server should start listening on all interfaces and the default port 8000.

On the client machine, run the following command to list all devices from which the gateway has received messages since the start:
```
riotee-gateway --host [SERVER] client
```
Replace [SERVER] with the hostname or IP of the server. If it is the same machine, you can omit the `--host` parameter.
