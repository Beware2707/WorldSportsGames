"""Structured JSON logging.

One JSON object per line so logs are queryable in any aggregator without a
custom parser. Two rules that matter more than the format:

- **Never log secrets.** Authorization headers, tokens, passwords and API
  keys are redacted by a filter rather than by asking every call site to
  remember.
- **Correlate, don't guess.** Every request carries a request id, and it is
  attached to every log line emitted while handling it.
"""

import json
import logging
import re
from contextvars import ContextVar
from typing import Any

# Set per request by RequestContextMiddleware; read by the formatter.
request_id_var: ContextVar[str | None] = ContextVar("request_id", default=None)

_SENSITIVE_KEYS = re.compile(
    r"(authorization|password|token|secret|api[_-]?key|cookie)", re.IGNORECASE
)
_BEARER = re.compile(r"(Bearer\s+)[A-Za-z0-9._\-]+", re.IGNORECASE)


def redact(value: Any) -> Any:
    """Strip anything that looks like a credential, at any nesting depth."""
    if isinstance(value, dict):
        return {
            key: ("[REDACTED]" if _SENSITIVE_KEYS.search(str(key)) else redact(val))
            for key, val in value.items()
        }
    if isinstance(value, list):
        return [redact(item) for item in value]
    if isinstance(value, str):
        return _BEARER.sub(r"\1[REDACTED]", value)
    return value


class JsonFormatter(logging.Formatter):
    def format(self, record: logging.LogRecord) -> str:
        payload: dict[str, Any] = {
            "level": record.levelname,
            "logger": record.name,
            "message": record.getMessage(),
            "time": self.formatTime(record, "%Y-%m-%dT%H:%M:%S%z"),
        }
        request_id = request_id_var.get()
        if request_id:
            payload["request_id"] = request_id
        if record.exc_info:
            payload["exception"] = self.formatException(record.exc_info)

        # Anything attached via logger.info(..., extra={"context": {...}}).
        context = getattr(record, "context", None)
        if isinstance(context, dict):
            payload["context"] = redact(context)

        return json.dumps(redact(payload), default=str)


def configure_logging(level: str = "INFO") -> None:
    handler = logging.StreamHandler()
    handler.setFormatter(JsonFormatter())

    root = logging.getLogger()
    root.handlers = [handler]
    root.setLevel(level.upper())

    # uvicorn ships its own handlers; route them through ours so every line
    # in the process has the same shape.
    for name in ("uvicorn", "uvicorn.error", "uvicorn.access"):
        logger = logging.getLogger(name)
        logger.handlers = []
        logger.propagate = True
