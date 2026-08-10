"""Deterministic insight provider.

The default provider computes insights arithmetically from recorded results.
It is not a language model: it cannot hallucinate a medal that was never won,
it needs no API key, and it produces the same text for the same data.

That makes it the honest default. An LLM provider (see ``llm.py``) is opt-in
and, when configured, is given the same pre-computed context — so its output
is grounded in the same facts rather than in its own recall.
"""

from statistics import mean
from typing import Any

from app.ai.base import AIInsight, InsightKind

_MEDAL_ORDER = {"gold": 0, "silver": 1, "bronze": 2}


class DeterministicProvider:
    name = "deterministic"

    async def generate(self, kind: InsightKind, context: dict[str, Any]) -> AIInsight:
        match kind:
            case InsightKind.ATHLETE_SUMMARY:
                return self._athlete_summary(context)
            case InsightKind.PERFORMANCE_TREND:
                return self._performance_trend(context)
            case InsightKind.HEAD_TO_HEAD:
                return self._head_to_head(context)
            case InsightKind.EVENT_PREVIEW:
                return self._event_preview(context)
            case InsightKind.RESULT_EXPLANATION:
                return self._result_explanation(context)
            case InsightKind.RULES_EXPLANATION:
                return self._rules_explanation(context)
            case _:
                return AIInsight(
                    kind=kind,
                    text="No insight is available for this request yet.",
                    provider=self.name,
                    basis=[],
                )

    # ---- factual summaries -------------------------------------------------

    def _athlete_summary(self, ctx: dict) -> AIInsight:
        name = ctx.get("name", "This athlete")
        country = ctx.get("country")
        disciplines = ctx.get("disciplines") or []
        medals = ctx.get("medals") or []
        records = ctx.get("records") or []
        results = ctx.get("results") or []

        parts = [name]
        if country:
            parts.append(f"competes for {country}")
        if disciplines:
            parts.append(f"in {_join(disciplines)}")
        sentence = " ".join(parts) + "."

        lines = [sentence]
        if medals:
            counts: dict[str, int] = {}
            for metal in medals:
                counts[metal] = counts.get(metal, 0) + 1
            tally = ", ".join(
                f"{counts[m]} {m}"
                for m in sorted(counts, key=lambda m: _MEDAL_ORDER.get(m, 9))
            )
            lines.append(f"Medals recorded: {tally}.")
        if records:
            lines.append(f"Holds {_join(records)}.")
        if results:
            wins = sum(1 for r in results if r == 1)
            podiums = sum(1 for r in results if r is not None and r <= 3)
            lines.append(
                f"In the last {len(results)} recorded results: {wins} win(s) "
                f"and {podiums} podium finish(es)."
            )
        if len(lines) == 1:
            lines.append("No results have been recorded yet.")

        return AIInsight(
            kind=InsightKind.ATHLETE_SUMMARY,
            text=" ".join(lines),
            provider=self.name,
            basis=["recorded medals", "recorded records", "recent results"],
            confidence="high",
        )

    def _result_explanation(self, ctx: dict) -> AIInsight:
        status = ctx.get("status")
        explanations = {
            "DNS": "DNS means Did Not Start — the athlete was entered but did "
            "not begin the event.",
            "DNF": "DNF means Did Not Finish — the athlete started but did not "
            "complete the event.",
            "DSQ": "DSQ means Disqualified — the result was voided under the "
            "rules of the event.",
        }
        if status in explanations:
            text = explanations[status]
        else:
            position = ctx.get("position")
            value = ctx.get("value_text")
            kind = ctx.get("value_kind", "result")
            bits = []
            if position:
                bits.append(f"Finished in position {position}")
            if value:
                bits.append(f"with a recorded {kind} of {value}")
            text = (" ".join(bits) + ".") if bits else "No result was recorded."
        return AIInsight(
            kind=InsightKind.RESULT_EXPLANATION,
            text=text,
            provider=self.name,
            basis=["recorded result"],
            confidence="high",
        )

    def _rules_explanation(self, ctx: dict) -> AIInsight:
        sport = ctx.get("sport", "this sport")
        discipline = ctx.get("discipline")
        subject = discipline or sport
        return AIInsight(
            kind=InsightKind.RULES_EXPLANATION,
            text=(
                f"Detailed rules for {subject} are not available in this "
                "release. Refer to the governing body's official rulebook."
            ),
            provider=self.name,
            basis=[],
            confidence="low",
        )

    # ---- estimates (always labelled) --------------------------------------

    def _performance_trend(self, ctx: dict) -> AIInsight:
        values = [v for v in (ctx.get("values") or []) if isinstance(v, int | float)]
        lower_is_better = bool(ctx.get("lower_is_better"))
        if len(values) < 2:
            return AIInsight(
                kind=InsightKind.PERFORMANCE_TREND,
                text="Not enough recorded results to describe a trend.",
                provider=self.name,
                basis=["recent results"],
                confidence="low",
            )

        half = max(1, len(values) // 2)
        earlier, later = mean(values[:half]), mean(values[half:])
        change = abs(later - earlier)

        # A flat series is steady, not declining. Treating "not improving" as
        # "declining" reported a perfectly consistent athlete as getting worse.
        epsilon = max(abs(earlier), 1e-9) * 1e-6
        if change <= epsilon:
            direction = "steady"
        elif (later < earlier) if lower_is_better else (later > earlier):
            direction = "improving"
        else:
            direction = "declining"

        return AIInsight(
            kind=InsightKind.PERFORMANCE_TREND,
            text=(
                f"Across {len(values)} recorded results the trend appears "
                f"{direction}: the recent average is {later:.2f} against "
                f"{earlier:.2f} earlier (a change of {change:.2f}). "
                "Small samples move a lot — treat this as a rough signal."
            ),
            provider=self.name,
            basis=[f"{len(values)} recorded results"],
            confidence="low" if len(values) < 5 else "medium",
        )

    def _head_to_head(self, ctx: dict) -> AIInsight:
        a, b = ctx.get("a", {}), ctx.get("b", {})
        meetings = ctx.get("meetings") or []
        a_name = a.get("name", "Athlete A")
        b_name = b.get("name", "Athlete B")

        if not meetings:
            return AIInsight(
                kind=InsightKind.HEAD_TO_HEAD,
                text=(
                    f"{a_name} and {b_name} have no recorded meetings, so no "
                    "head-to-head comparison can be made from the data held."
                ),
                provider=self.name,
                basis=["shared events"],
                confidence="low",
            )

        a_wins = sum(1 for m in meetings if m.get("winner") == "a")
        b_wins = sum(1 for m in meetings if m.get("winner") == "b")
        undecided = len(meetings) - a_wins - b_wins
        leader = a_name if a_wins > b_wins else b_name if b_wins > a_wins else None
        verdict = (
            f"{leader} leads {max(a_wins, b_wins)}–{min(a_wins, b_wins)}"
            if leader
            else f"They are level at {a_wins}–{b_wins}"
        )
        # State the undecided count so the tally reconciles with the meeting
        # count; silently dropping them made the numbers appear not to add up.
        remainder = (
            f" {undecided} meeting(s) had no decisive placing." if undecided else ""
        )
        return AIInsight(
            kind=InsightKind.HEAD_TO_HEAD,
            text=(
                f"In {len(meetings)} recorded meeting(s), {verdict}.{remainder} "
                "Past meetings do not determine future ones."
            ),
            provider=self.name,
            basis=[f"{len(meetings)} shared events"],
            confidence="low" if len(meetings) < 3 else "medium",
        )

    def _event_preview(self, ctx: dict) -> AIInsight:
        event = ctx.get("event_name", "This event")
        entrants = ctx.get("entrants") or []
        ranked = [e for e in entrants if e.get("rank")]
        ranked.sort(key=lambda e: e["rank"])

        if not entrants:
            text = f"{event} has no entrants recorded yet."
        elif ranked:
            names = _join([f"{e['name']} (ranked {e['rank']})" for e in ranked[:3]])
            text = (
                f"{event} features {len(entrants)} entrant(s). On current "
                f"ranking, {names} start among the favourites — rankings "
                "reflect past form, not this race."
            )
        else:
            text = (
                f"{event} features {len(entrants)} entrant(s). None of them "
                "carry a ranking in the data held, so no favourite can be "
                "identified."
            )
        return AIInsight(
            kind=InsightKind.EVENT_PREVIEW,
            text=text,
            provider=self.name,
            basis=["entry list", "published rankings"],
            confidence="low",
        )


def _join(items: list[str]) -> str:
    items = [str(i) for i in items]
    if not items:
        return ""
    if len(items) == 1:
        return items[0]
    return ", ".join(items[:-1]) + f" and {items[-1]}"
