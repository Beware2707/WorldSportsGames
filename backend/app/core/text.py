"""Text helpers shared by every user-supplied-query code path."""

LIKE_ESCAPE = "\\"


def like_pattern(q: str) -> str:
    """Build a contains-pattern with LIKE wildcards escaped.

    Without this, a query of "%" matches everything and "_" can be used to
    probe values character by character. Parameters are bound either way, so
    this is pattern injection, not SQL injection — but it is still a way to
    make the API answer questions it was not asked.

    Must be paired with ``ilike(pattern, escape=LIKE_ESCAPE)``.
    """
    escaped = (
        q.replace(LIKE_ESCAPE, LIKE_ESCAPE * 2)
        .replace("%", f"{LIKE_ESCAPE}%")
        .replace("_", f"{LIKE_ESCAPE}_")
    )
    return f"%{escaped}%"
