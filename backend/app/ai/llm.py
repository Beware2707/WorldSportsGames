"""Optional LLM-backed provider.

Deliberately opt-in and vendor-neutral: the endpoint, model and key all come
from configuration, so pointing at a different vendor is a config change.

Two safeguards matter more than the wiring:

- The model is given the **same pre-computed context** the deterministic
  provider uses, and is instructed to use only that. It is a phrasing layer
  over facts we already hold, not a source of facts.
- Its output still goes through ``AIInsight``, so predictive kinds are
  labelled as estimates no matter what the model wrote.

If the call fails for any reason the caller falls back to the deterministic
provider — an outage degrades quality, never correctness.
"""

import json
import logging
from typing import Any

import httpx

from app.ai.base import AIInsight, InsightKind

logger = logging.getLogger(__name__)

_SYSTEM_PROMPT = (
    "You write short, factual sports insights for a sports app. "
    "Use ONLY the facts in the provided context: never add results, medals, "
    "records or dates that are not present. If the context is insufficient, "
    "say so plainly. Never state a future outcome as certain. "
    "Reply with two or three sentences of plain prose and nothing else."
)


class LLMProvider:
    name = "llm"

    def __init__(
        self,
        *,
        endpoint: str,
        api_key: str,
        model: str,
        timeout_seconds: float = 8.0,
    ) -> None:
        self._endpoint = endpoint
        self._api_key = api_key
        self._model = model
        self._timeout = timeout_seconds

    async def generate(self, kind: InsightKind, context: dict[str, Any]) -> AIInsight:
        payload = {
            "model": self._model,
            "max_tokens": 300,
            "system": _SYSTEM_PROMPT,
            "messages": [
                {
                    "role": "user",
                    "content": (
                        f"Insight type: {kind.value}\n"
                        f"Context (the only facts you may use):\n"
                        f"{json.dumps(context, default=str, indent=2)}"
                    ),
                }
            ],
        }
        async with httpx.AsyncClient(timeout=self._timeout) as client:
            response = await client.post(
                self._endpoint,
                json=payload,
                headers={
                    "x-api-key": self._api_key,
                    "anthropic-version": "2023-06-01",
                    "content-type": "application/json",
                },
            )
            response.raise_for_status()
            body = response.json()

        text = _extract_text(body)
        if not text:
            raise ValueError("LLM returned no usable text")

        # Labelling is enforced by AIInsight, not by trusting the model.
        return AIInsight(
            kind=kind,
            text=text,
            provider=self.name,
            basis=sorted(context.keys()),
            confidence="medium",
        )


def _extract_text(body: dict) -> str:
    """Pull prose out of a messages-style response without assuming a shape."""
    blocks = body.get("content")
    if isinstance(blocks, list):
        parts = [b.get("text", "") for b in blocks if isinstance(b, dict)]
        return "\n".join(p for p in parts if p).strip()
    if isinstance(body.get("text"), str):
        return body["text"].strip()
    return ""
