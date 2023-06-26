import click
import logging
import uvicorn
from riotee_gateway.client import GatewayClient
import riotee_gateway.server
from riotee_gateway import Transceiver
import time
import signal
import sys
import json


@click.option("-v", "--verbose", count=True, default=2)
@click.group(context_settings=dict(help_option_names=["-h", "--help"], obj={}))
@click.pass_context
def cli(ctx, verbose):
    if verbose == 0:
        logging.basicConfig(level=logging.ERROR)
    elif verbose == 1:
        logging.basicConfig(level=logging.WARNING)
    elif verbose == 2:
        logging.basicConfig(level=logging.INFO)
    elif verbose > 2:
        logging.basicConfig(level=logging.DEBUG)


@cli.command(short_help="server stuff")
@click.option(
    "--device",
    "-d",
    type=click.Path(exists=True),
    required=False,
    help="Path to USB device (/dev/ttyACMx or COMX)",
)
@click.option("-p", "--port", type=int, default=8000, help="Port for API server")
@click.option("-h", "--host", type=str, default="0.0.0.0", help="Host for API server")
@click.pass_context
def server(ctx, device, port, host):
    riotee_gateway.server.tcv = Transceiver(port=device)
    uvicorn.run("riotee_gateway.server:app", port=port, host=host)


@cli.group(short_help="client stuff")
@click.option("-p", "--port", type=int, default=8000, help="Port for API server")
@click.option("-h", "--host", type=str, default="localhost", help="Host for API server")
@click.pass_context
def client(ctx, host, port):
    ctx.obj["client"] = GatewayClient(host, port)


@client.command(short_help="fetch packets from the server")
@click.option("-d", "--device", type=str, required=True)
@click.option("-o", "--output", type=click.Path())
@click.pass_context
def fetch(ctx, device, output):
    if output:
        f = open(output, "w+")

    for pkt in ctx.obj["client"].pops(device):
        click.echo(pkt.to_json())
        if output:
            f.writelines(json.dumps(pkt.to_json()) + "\n")

    if output:
        f.close()


@client.command(short_help="list visible devices")
@click.pass_context
def devices(ctx):
    for dev in ctx.obj["client"].get_devices():
        click.echo(dev)


@client.command(short_help="send ascii message to device")
@click.option("-d", "--device", type=str)
@click.option("-m", "--message", type=str)
@click.pass_context
def send(ctx, device, message):
    ctx.obj["client"].send_ascii(device, message)


@client.command(short_help="continuously poll the server for packets")
@click.option("-d", "--device", type=str, required=True)
@click.option("-i", "--interval", type=float, default=0.1)
@click.option("-o", "--output", type=click.Path())
@click.pass_context
def monitor(ctx, device, interval, output):
    if output:
        f = open(output, "w+")

    def stop_loop(signum, frame):
        if output:
            f.close()
        sys.exit(0)

    signal.signal(signal.SIGINT, stop_loop)

    while True:
        for pkt in ctx.obj["client"].pops(device):
            click.echo(pkt.to_json())
            if output:
                f.writelines(json.dumps(pkt.to_json()) + "\n")

        time.sleep(interval)


if __name__ == "__main__":
    cli()
