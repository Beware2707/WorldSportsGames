from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import AppUser


async def get_user_by_email(session: AsyncSession, email: str) -> AppUser | None:
    result = await session.execute(select(AppUser).where(AppUser.email == email.lower()))
    return result.scalar_one_or_none()


async def get_user_by_id(session: AsyncSession, user_id: int) -> AppUser | None:
    return await session.get(AppUser, user_id)


async def create_user(
    session: AsyncSession, email: str, hashed_password: str, display_name: str
) -> AppUser:
    user = AppUser(email=email.lower(), hashed_password=hashed_password, display_name=display_name)
    session.add(user)
    await session.commit()
    await session.refresh(user)
    return user
