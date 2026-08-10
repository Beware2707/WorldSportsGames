from sqlalchemy import select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import AppUser


async def get_user_by_email(session: AsyncSession, email: str) -> AppUser | None:
    result = await session.execute(select(AppUser).where(AppUser.email == email.lower()))
    return result.scalar_one_or_none()


async def get_user_by_id(session: AsyncSession, user_id: int) -> AppUser | None:
    return await session.get(AppUser, user_id)


class EmailAlreadyRegistered(Exception):
    """Raised when the unique constraint on app_user.email rejects an insert."""


async def create_user(
    session: AsyncSession, email: str, hashed_password: str, display_name: str
) -> AppUser:
    user = AppUser(email=email.lower(), hashed_password=hashed_password, display_name=display_name)
    session.add(user)
    try:
        await session.commit()
    except IntegrityError as exc:
        # Concurrent duplicate registration: the check-then-insert in the
        # router loses the race here. Surface it as a conflict, not a 500.
        await session.rollback()
        raise EmailAlreadyRegistered from exc
    await session.refresh(user)
    return user
