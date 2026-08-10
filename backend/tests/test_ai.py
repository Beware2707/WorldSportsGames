"""AI abstraction tests.

The load-bearing property is that generated text can never reach a client
without being labelled — enforced by construction, not by remembering.
"""

import pytest

from app.ai.base import PREDICTIVE_KINDS, AIInsight, AIProvider, InsightKind
from app.ai.deterministic import DeterministicProvider
from app.schemas.ai import AIInsightOut


def test_predictive_kinds_are_forced_to_estimate():
    """Even a caller that explicitly says is_estimate=False cannot win."""
    for kind in PREDICTIVE_KINDS:
        insight = AIInsight(
            kind=kind, text="x", provider="t", is_estimate=False, disclaimer=""
        )
        assert insight.is_estimate is True, f"{kind} escaped the estimate label"
        assert insight.disclaimer
        assert "estimate" in insight.disclaimer.lower()


def test_factual_kinds_still_carry_a_disclaimer():
    insight = AIInsight(
        kind=InsightKind.ATHLETE_SUMMARY, text="x", provider="t"
    )
    assert insight.is_estimate is False
    assert insight.disclaimer, "even derived text must say where it came from"


def test_schema_rejects_an_empty_disclaimer():
    """The wire format cannot carry unlabelled generated text."""
    with pytest.raises(ValueError):
        AIInsightOut(
            kind="prediction",
            text="Athlete X will win.",
            provider="t",
            is_estimate=True,
            disclaimer="   ",
            basis=[],
            confidence="low",
        )


def test_deterministic_provider_satisfies_the_protocol():
    assert isinstance(DeterministicProvider(), AIProvider)


async def test_summary_uses_only_supplied_facts():
    provider = DeterministicProvider()
    insight = await provider.generate(
        InsightKind.ATHLETE_SUMMARY,
        {
            "name": "Zellie Dunbar",
            "country": "Jamaica",
            "disciplines": ["Track & Field"],
            "medals": ["gold", "gold", "silver"],
            "records": ["WR in 100m"],
            "results": [1, 1, 2],
        },
    )
    assert "Zellie Dunbar" in insight.text
    assert "2 gold" in insight.text and "1 silver" in insight.text
    assert insight.is_estimate is False


async def test_summary_of_an_athlete_with_no_data_says_so():
    insight = await DeterministicProvider().generate(
        InsightKind.ATHLETE_SUMMARY, {"name": "Nobody"}
    )
    assert "No results have been recorded" in insight.text


async def test_trend_respects_score_direction():
    provider = DeterministicProvider()
    # Times: falling values are an improvement.
    faster = await provider.generate(
        InsightKind.PERFORMANCE_TREND,
        {"values": [11.0, 10.9, 10.8, 10.7], "lower_is_better": True},
    )
    assert "improving" in faster.text

    # Points: the same falling series is a decline.
    fewer = await provider.generate(
        InsightKind.PERFORMANCE_TREND,
        {"values": [11.0, 10.9, 10.8, 10.7], "lower_is_better": False},
    )
    assert "declining" in fewer.text
    assert fewer.is_estimate is True


async def test_trend_refuses_to_extrapolate_from_one_point():
    insight = await DeterministicProvider().generate(
        InsightKind.PERFORMANCE_TREND, {"values": [10.5], "lower_is_better": True}
    )
    assert "Not enough recorded results" in insight.text
    assert insight.confidence == "low"


async def test_head_to_head_with_no_meetings_is_honest():
    insight = await DeterministicProvider().generate(
        InsightKind.HEAD_TO_HEAD,
        {"a": {"name": "A"}, "b": {"name": "B"}, "meetings": []},
    )
    assert "no recorded meetings" in insight.text
    assert insight.is_estimate is True


async def test_result_explanation_covers_non_finishes():
    provider = DeterministicProvider()
    for status, expected in [
        ("DNS", "Did Not Start"),
        ("DNF", "Did Not Finish"),
        ("DSQ", "Disqualified"),
    ]:
        insight = await provider.generate(
            InsightKind.RESULT_EXPLANATION, {"status": status}
        )
        assert expected in insight.text


# ---- endpoints -----------------------------------------------------------


async def test_every_ai_endpoint_labels_its_output(client):
    endpoints = [
        "/api/v1/ai/athletes/zellie-dunbar/summary",
        "/api/v1/ai/athletes/zellie-dunbar/trend",
        "/api/v1/ai/head-to-head?a=zellie-dunbar&b=amara-okafor",
        "/api/v1/ai/explain/result?status=DNF",
    ]
    for endpoint in endpoints:
        r = await client.get(endpoint)
        assert r.status_code == 200, endpoint
        body = r.json()
        assert body["disclaimer"].strip(), f"{endpoint} returned no disclaimer"
        assert body["provider"] == "deterministic"


async def test_predictive_endpoints_are_marked_as_estimates(client):
    trend = (await client.get("/api/v1/ai/athletes/zellie-dunbar/trend")).json()
    assert trend["is_estimate"] is True

    h2h = (
        await client.get(
            "/api/v1/ai/head-to-head", params={"a": "zellie-dunbar", "b": "amara-okafor"}
        )
    ).json()
    assert h2h["is_estimate"] is True

    summary = (
        await client.get("/api/v1/ai/athletes/zellie-dunbar/summary")
    ).json()
    assert summary["is_estimate"] is False, "a factual summary is not a forecast"


async def test_head_to_head_reflects_real_meetings(client):
    r = await client.get(
        "/api/v1/ai/head-to-head",
        params={"a": "zellie-dunbar", "b": "amara-okafor"},
    )
    body = r.json()
    # They met in the Paris 100m final, where Dunbar won.
    assert "1 recorded meeting" in body["text"]
    assert "Zellie Dunbar leads" in body["text"]


async def test_head_to_head_rejects_comparing_an_athlete_with_themselves(client):
    r = await client.get(
        "/api/v1/ai/head-to-head",
        params={"a": "zellie-dunbar", "b": "zellie-dunbar"},
    )
    assert r.status_code == 422


async def test_ai_endpoints_404_on_unknown_entities(client):
    assert (
        await client.get("/api/v1/ai/athletes/nobody/summary")
    ).status_code == 404
    assert (await client.get("/api/v1/ai/events/999999/preview")).status_code == 404


async def test_provider_failure_falls_back_rather_than_erroring(client, test_app):
    """An LLM outage must degrade quality, not break the endpoint."""

    class BrokenProvider:
        name = "broken"

        async def generate(self, kind, context):
            raise RuntimeError("upstream is down")

    test_app.state.ai_provider = BrokenProvider()
    r = await client.get("/api/v1/ai/athletes/zellie-dunbar/summary")
    assert r.status_code == 200
    body = r.json()
    assert body["provider"] == "deterministic"
    assert body["disclaimer"]
