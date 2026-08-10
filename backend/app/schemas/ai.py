from pydantic import BaseModel, Field, field_validator


class AIInsightOut(BaseModel):
    """Wire format for a generated insight.

    ``disclaimer`` is non-optional and validated to be non-empty: a client
    cannot receive generated text without the caveat that belongs with it.
    """

    kind: str
    text: str
    provider: str
    is_estimate: bool
    disclaimer: str = Field(min_length=1)
    basis: list[str]
    confidence: str

    @field_validator("disclaimer")
    @classmethod
    def _must_disclaim(cls, value: str) -> str:
        if not value.strip():
            raise ValueError("generated insights must carry a disclaimer")
        return value
