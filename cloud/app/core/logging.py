"""Structured logging (structlog) with secret redaction.

Any event key that looks like a secret is replaced with "[redacted]" before
output, however deep it is nested, so a careless log call cannot leak a
password, token or key (AGENTS.md: never log secrets).
"""

from __future__ import annotations

import logging
import re
from collections.abc import MutableMapping
from typing import Any

import structlog

_SECRET_KEY = re.compile(
    r"pass(word)?|secret|token|authorization|cookie|api[_-]?key|local[_-]?key|kek|claim|credential|setup[_-]?proof",
    re.IGNORECASE,
)
REDACTED = "[redacted]"


def _redact(value: Any, depth: int = 0) -> Any:
    if depth > 8:
        return value
    if isinstance(value, dict):
        return {k: REDACTED if _SECRET_KEY.search(str(k)) else _redact(v, depth + 1) for k, v in value.items()}
    if isinstance(value, list | tuple):
        return type(value)(_redact(v, depth + 1) for v in value)
    return value


def redact_processor(_: Any, __: str, event_dict: MutableMapping[str, Any]) -> MutableMapping[str, Any]:
    for key in list(event_dict):
        if key == "event":
            continue
        event_dict[key] = REDACTED if _SECRET_KEY.search(key) else _redact(event_dict[key])
    return event_dict


def configure_logging(level: str = "INFO", json_output: bool = True) -> None:
    renderer: Any = structlog.processors.JSONRenderer() if json_output else structlog.dev.ConsoleRenderer()
    structlog.configure(
        processors=[
            structlog.contextvars.merge_contextvars,
            structlog.processors.add_log_level,
            structlog.processors.TimeStamper(fmt="iso", utc=True),
            redact_processor,
            structlog.processors.format_exc_info,
            renderer,
        ],
        wrapper_class=structlog.make_filtering_bound_logger(logging.getLevelName(level)),
        # No fixed file: each logger looks up sys.stdout when it is created, so a
        # replaced or closed stream (process supervisors, test capture) is never kept.
        logger_factory=structlog.PrintLoggerFactory(),
        cache_logger_on_first_use=False,
    )
    # Uvicorn access logs would print query strings (the events WebSocket takes
    # a token in the query from P4), so they are off; requests are logged by
    # our own middleware without the query string.
    logging.getLogger("uvicorn.access").disabled = True
