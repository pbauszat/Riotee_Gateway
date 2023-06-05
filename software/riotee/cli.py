import click
import logging
import uvicorn
from riotee.client import GatewayConnection


@click.option("-v", "--verbose", count=True, default=2)
@click.option("-p", "--port", type=int, default=8000, help="Port for API server")
@click.option("-h", "--host", type=str, default="0.0.0.0", help="Host for API server")
@click.group(context_settings=dict(help_option_names=["-h", "--help"], obj={}))
@click.pass_context
def cli(ctx, host, port, verbose):
    if verbose == 0:
        logging.basicConfig(level=logging.ERROR)
    elif verbose == 1:
        logging.basicConfig(level=logging.WARNING)
    elif verbose == 2:
        logging.basicConfig(level=logging.INFO)
    elif verbose > 2:
        logging.basicConfig(level=logging.DEBUG)

    ctx.obj["host"] = host
    ctx.obj["port"] = port


@cli.command(short_help="server stuff")
@click.pass_context
def server(ctx):
    click.echo("Here to serve")
    uvicorn.run("riotee.server:app", port=ctx.obj["port"], host=ctx.obj["host"])


@cli.group(short_help="client stuff")
@click.pass_context
def client(ctx):
    ctx.obj["client"] = GatewayConnection(ctx.obj["host"], ctx.obj["port"])


@client.command(short_help="fetch packets from the server")
@click.option("-d", "--device", type=str, required=False)
@click.pass_context
def fetch(ctx, device):
    click.echo("Fetching stuff")


@client.command(short_help="list visible devices")
@click.pass_context
def devices(ctx):
    devices = ctx.obj["client"].get_devices()


@client.command(short_help="send ascii message to device")
@click.option("-d", "--device", type=str)
@click.option("-m", "--message", type=str)
def send(device, message):
    click.echo(f"sending {message} to {device}")


if __name__ == "__main__":
    cli()
