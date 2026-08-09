from typing import Any

from pydantic import BaseModel


class HomeSection(BaseModel):
    """One renderable section of the home feed.

    ``kind`` drives which client renderer is used; ``items`` payload shape is
    kind-specific. The server owns section composition and ordering so the feed
    can become personalized without client releases.
    """

    kind: str
    title: str
    items: list[dict[str, Any]]


class HomeFeed(BaseModel):
    sections: list[HomeSection]
