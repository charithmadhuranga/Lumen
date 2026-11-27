"""Run the public and internal apps in one process (the container entry point).

python -m app.serve
"""

from __future__ import annotations

import asyncio

import uvicorn
from fastapi import FastAPI

from app.core.settings import get_settings
from app.main import Metrics, Resources, create_internal_app, create_public_app


async def main() -> None:
    settings = get_settings()
    metrics = Metrics()
    resources = Resources(settings)
    public = create_public_app(settings, metrics, resources)
    internal = create_internal_app(settings, metrics, resources)

    def config(app: FastAPI, host: str, port: int) -> uvicorn.Config:
        return uvicorn.Config(
            app, host=host, port=port, log_config=None, proxy_headers=True, forwarded_allow_ips="*", server_header=False
        )

    servers = [
        uvicorn.Server(config(public, settings.public_host, settings.public_port)),
        uvicorn.Server(config(internal, settings.internal_host, settings.internal_port)),
    ]
    try:
        await asyncio.gather(*(s.serve() for s in servers))
    finally:
        await resources.close()


if __name__ == "__main__":
    asyncio.run(main())
