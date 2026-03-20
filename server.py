#!/usr/bin/env python3

import asyncio
import logging

import aiocoap
import aiocoap.resource as resource

logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s %(levelname)s %(name)s — %(message)s")
log = logging.getLogger("coap_led_server")


class LedResource(resource.ObservableResource):
    def __init__(self):
        super().__init__()
        self._state: int = 0

    async def render_get(self, request: aiocoap.Message) -> aiocoap.Message:
        payload = str(self._state).encode()
        return aiocoap.Message(
            code=aiocoap.CONTENT,
            payload=payload,
            content_format=aiocoap.numbers.media_types_rev["text/plain"],
        )

    async def render_put(self, request: aiocoap.Message) -> aiocoap.Message:
        body = request.payload.decode(errors="ignore").strip()
        if body not in ("0", "1"):
            log.warning("PUT /led — invalid payload %r", body)
            return aiocoap.Message(code=aiocoap.BAD_REQUEST,
                                   payload=b"expected '0' or '1'")

        new_state = int(body)
        if new_state != self._state:
            self._state = new_state
            log.info("LED → %s — notifying observers", "ON" if self._state else "OFF")
            self.updated_state()
        else:
            log.info("LED already %s — no-op", "ON" if self._state else "OFF")

        return aiocoap.Message(code=aiocoap.CHANGED)

async def main():
    root = resource.Site()
    root.add_resource(["led"], LedResource())

    await aiocoap.Context.create_server_context(root,
                                                bind=("0.0.0.0", 5683))
    log.info("CoAP LED server listening on 0.0.0.0:5683")
    log.info("  GET  coap://<host>/led")
    log.info("  PUT  coap://<host>/led   payload: 0 or 1")

    await asyncio.get_running_loop().create_future()

if __name__ == "__main__":
    asyncio.run(main())
