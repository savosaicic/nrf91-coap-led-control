# nrf91-coap-led-control

Control a LED on an nRF9151dk over LTE-M using CoAP Observe.

## How it works

The board connects to a CoAP server and subscribes to a `/led` resource using
the Observe option. Whenever a client updates the resource on the server,
the server pushes the new value to all observers including the board, which
applies it to the LED.

## Prerequisites

### Hardware
- A nRF9151 board with a SIM card - or use native_sim to run the firmware on
  your local machine

### Software
- Zephyr SDK toolchain
  (follow the [Getting Started guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html))

### native_sim only

- [Zephyr net-tools](https://github.com/zephyrproject-rtos/net-tools)

## Server

```bash
pip install aiocoap
python server.py
```

Control the LED from a CoAP client:

```bash
coap-client -m put coap://<server-ip>/led -e "1" # on
coap-client -m put coap://<server-ip>/led -e "0" # off
coap-client -m get coap://<server-ip>/led        # read
```

## Firmware

### 1. Initialise the workspace

```bash
mkdir nrf91-coap-led-control-ws && cd nrf91-coap-led-control-ws
python3 -m venv .venv
source .venv/bin/activate
pip install west
west init -m https://github.com/savosaicic/nrf91-coap-led-control --mr main
west update
pip install -r zephyr/scripts/requirements.txt
```

### 2. Set your server address in `prj.conf`:

```
CONFIG_COAP_SERVER_HOSTNAME="your-domain.com"
```

### Build & flash

#### nRF9151 DK

```bash
west build -b nrf9151dk/nrf9151/ns .
west flash
```

#### native_sim

Set up the TAP interface using Zephyr net-tools:

```bash
cd net-tools
sudo ./net-setup.sh
```

Build and run:

```bash
west build -b native_sim . -p
./build/nrf91-coap-led-control/zephyr/zephyr.exe
```
