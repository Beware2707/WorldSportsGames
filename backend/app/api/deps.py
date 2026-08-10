from typing import Annotated

from fastapi import Depends, HTTPException, status
from fastapi.security import OAuth2PasswordBearer
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.config import get_settings
from app.core.security import decode_access_token
from app.db.session import get_db
from app.models import AppUser
from app.repositories.user import get_user_by_id

oauth2_scheme = OAuth2PasswordBearer(tokenUrl="/api/v1/auth/login")
# auto_error=False: endpoints that personalize for signed-in users but stay
# fully usable anonymously (the home feed) must not 401 without a token.
optional_oauth2_scheme = OAuth2PasswordBearer(
    tokenUrl="/api/v1/auth/login", auto_error=False
)

DbSession = Annotated[AsyncSession, Depends(get_db)]


async def get_current_user(
    session: DbSession, token: Annotated[str, Depends(oauth2_scheme)]
) -> AppUser:
    subject = decode_access_token(token)
    user = None
    if subject is not None and subject.isdigit():
        user = await get_user_by_id(session, int(subject))
    if user is None or not user.is_active:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or expired credentials",
            headers={"WWW-Authenticate": "Bearer"},
        )
    return user


CurrentUser = Annotated[AppUser, Depends(get_current_user)]


async def get_optional_user(
    session: DbSession, token: Annotated[str | None, Depends(optional_oauth2_scheme)]
) -> AppUser | None:
    """Current user when a valid token is supplied, else None.

    An invalid or expired token is treated as anonymous rather than an error:
    personalized-but-public endpoints should degrade to the generic response.
    """
    if token is None:
        return None
    subject = decode_access_token(token)
    if subject is None or not subject.isdigit():
        return None
    user = await get_user_by_id(session, int(subject))
    return user if user is not None and user.is_active else None


OptionalUser = Annotated["AppUser | None", Depends(get_optional_user)]


def require_dev_mode() -> None:
    """Hide dev-only routes outside development.

    Declared as a route-level dependency so it resolves BEFORE authentication:
    in production the endpoint 404s for everyone, rather than advertising its
    existence with a 401.
    """
    settings = get_settings()
    if not (settings.debug and settings.enable_dev_fixtures):
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Not found")
