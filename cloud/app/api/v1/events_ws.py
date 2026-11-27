"""Live events for apps: WebSocket /api/v1/events (PROTOCOL.md §10).

Authentication: `?access_token=` (query strings are never logged) or, preferred,
a first message {"type": "auth", "accessToken": "..."} within 5 s. The
connection receives events only for the homes the user belongs to; homes and
the session are re-checked every 30 s, so leaving a home or signing out ends
delivery. Server messages: {"v":1,"event","ts","data"}; "hello" first, "ping"
every 25 s. A client that cannot keep up (100 queued events) is closed with
code 1013 and should reconnect and refetch.
"""

from __future__ import annotations

import asyncio
import contextlib
import json
import uuid
from typing import Any

from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from sqlalchemy import select

from app.core.security import TokenError, decode_access_token
from app.models.account import Session, User, UserStatus
from app.models.home import HomeMember
from app.services.events import EventHub, event_message

router = APIRouter()

AUTH_TIMEOUT_S = 5.0
PING_S = 25.0
RECHECK_S = 30.0
CLOSE_UNAUTHORIZED, CLOSE_SLOW = 4401, 1013


async def _homes(websocket: WebSocket, token: str) -> set[str] | None:
    """Home ids of a valid, live session; None when the token or session is not valid."""
    try:
        claims = decode_access_token(websocket.app.state.settings, token)
    except TokenError:
        return None
    async with websocket.app.state.sessionmaker() as db:
        row = (
            await db.execute(
                select(User.status, Session.revoked_at)
                .join(Session, Session.user_id == User.id)
                .where(User.id == claims.user_id, Session.id == claims.session_id)
            )
        ).first()
        if row is None or row.revoked_at is not None or row.status != UserStatus.active:
            return None
        homes = (await db.scalars(select(HomeMember.home_id).where(HomeMember.user_id == claims.user_id))).all()
    return {str(h) for h in homes}


async def _first_token(websocket: WebSocket) -> str | None:
    token = websocket.query_params.get("access_token")
    if token:
        return token
    try:
        raw = await asyncio.wait_for(websocket.receive_text(), AUTH_TIMEOUT_S)
        msg = json.loads(raw)
    except (TimeoutError, ValueError, WebSocketDisconnect):
        return None
    if isinstance(msg, dict) and msg.get("type") == "auth" and isinstance(msg.get("accessToken"), str):
        return str(msg["accessToken"])[:2048]
    return None


@router.websocket("/api/v1/events")
async def events(websocket: WebSocket) -> None:
    await websocket.accept()
    token = await _first_token(websocket)
    homes = await _homes(websocket, token) if token else None
    if homes is None:
        await websocket.close(code=CLOSE_UNAUTHORIZED, reason="unauthorized")
        return
    hub: EventHub = websocket.app.state.hub
    sub = hub.add(homes)
    await websocket.send_json(event_message("hello", {"homes": sorted(homes), "connectionId": uuid.uuid4().hex}))

    async def reader() -> None:
        # Clients may send {"type":"ping"}; everything else is ignored. Ends on disconnect.
        with contextlib.suppress(WebSocketDisconnect):
            while True:
                await websocket.receive_text()

    async def recheck() -> None:
        while True:
            await asyncio.sleep(RECHECK_S)
            current = await _homes(websocket, token) if token else None
            if current is None:
                raise PermissionError("session ended")
            sub.homes = current

    read_task = asyncio.create_task(reader())
    check_task = asyncio.create_task(recheck())
    close_code, close_reason = 1000, ""
    try:
        while not read_task.done() and not check_task.done():
            if sub.overflowed:
                close_code, close_reason = CLOSE_SLOW, "too slow; reconnect and refetch"
                break
            try:
                message: dict[str, Any] = await asyncio.wait_for(sub.queue.get(), PING_S)
            except TimeoutError:
                message = event_message("ping", {})
            await websocket.send_json(message)
        if check_task.done() and not check_task.cancelled() and check_task.exception() is not None:
            close_code, close_reason = CLOSE_UNAUTHORIZED, "session ended"
    except (WebSocketDisconnect, RuntimeError):
        pass
    finally:
        hub.remove(sub)
        for task in (read_task, check_task):
            task.cancel()
            with contextlib.suppress(asyncio.CancelledError, Exception):
                await task
        if not read_task.done() or close_code != 1000:
            with contextlib.suppress(RuntimeError, WebSocketDisconnect):
                await websocket.close(code=close_code, reason=close_reason)
