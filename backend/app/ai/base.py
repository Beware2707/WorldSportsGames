"""Provider-agnostic AI abstraction.

Two rules shape everything here:

1. **No coupling to one provider.** Callers depend on ``AIProvider``; swapping
   an LLM vendor is a config change, not a code change.
2. **Nothing generated may be presentable as fact.** ``AIInsight`` cannot be
   constructed without a disclaimer, and predictive kinds are forced to
   ``is_estimate=True`` by the constructor rather than by the caller
   remembering. A model that "sounds confident" is still a guess, and the
   payload says so on every response.
"""

from dataclasses import dataclass, field
from enum import StrEnum
from typing import Any, Protocol, runtime_checkable


class InsightKind(StrEnum):
    ATHLETE_SUMMARY = "athlete_summary"
    PERFORMANCE_TREND = "performance_trend"
    EVENT_PREVIEW = "event_preview"
    PREDICTION = "prediction"
    HEAD_TO_HEAD = "head_to_head"
    RESULT_EXPLANATION = "result_explanation"
    RULES_EXPLANATION = "rules_explanation"
    COMPETITION_SUMMARY = "competition_summary"


# Kinds that project beyond recorded fact. These can never be labelled
# anything other than an estimate.
PREDICTIVE_KINDS = frozenset(
    {
        InsightKind.PREDICTION,
        InsightKind.EVENT_PREVIEW,
        InsightKind.PERFORMANCE_TREND,
        InsightKind.HEAD_TO_HEAD,
    }
)

_ESTIMATE_DISCLAIMER = (
    "This is an automated estimate based on past results, not a prediction of "
    "what will happen."
)
_DERIVED_DISCLAIMER = (
    "Automatically generated from recorded results. Check official sources for "
    "the authoritative record."
)


@dataclass(frozen=True)
class AIInsight:
    """One generated insight.

    ``is_estimate`` and ``disclaimer`` are derived in ``__post_init__``, so no
    call site can produce an unlabelled prediction — not even by accident.
    """

    kind: InsightKind
    text: str
    provider: str
    is_estimate: bool = field(default=False)
    disclaimer: str = field(default="")
    # What the insight was computed from, so the UI can show its working.
    basis: list[str] = field(default_factory=list)
    confidence: str = "unknown"  # low | medium | high | unknown

    def __post_init__(self) -> None:
        forced_estimate = self.kind in PREDICTIVE_KINDS
        object.__setattr__(self, "is_estimate", self.is_estimate or forced_estimate)
        if not self.disclaimer:
            object.__setattr__(
                self,
                "disclaimer",
                _ESTIMATE_DISCLAIMER if self.is_estimate else _DERIVED_DISCLAIMER,
            )


@runtime_checkable
class AIProvider(Protocol):
    """Contract every AI backend implements.

    ``context`` is already-normalized domain data — providers never query the
    database themselves, so a hosted model never sees more than it was given.
    """

    name: str

    async def generate(self, kind: InsightKind, context: dict[str, Any]) -> AIInsight: ...


class AIUnavailable(RuntimeError):
    """Raised when no provider can serve a request."""


_registry: dict[str, AIProvider] = {}


def register_provider(provider: AIProvider) -> None:
    _registry[provider.name] = provider


def get_provider(name: str) -> AIProvider | None:
    return _registry.get(name)


def available_providers() -> list[str]:
    return sorted(_registry)
