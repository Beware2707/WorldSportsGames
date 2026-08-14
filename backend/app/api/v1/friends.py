import secrets
import string
from datetime import UTC, datetime

from fastapi import APIRouter, HTTPException, Response, status
from sqlalchemy import or_, select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.api.deps import CurrentUser, DbSession
from app.models import FriendCode, Friendship
from app.schemas.social import FriendCodeOut, FriendOut, FriendRequestIn

router = APIRouter(prefix="/users/me/friends", tags=["friends"])

_CODE_ALPHABET = string.ascii_uppercase + string.digits
_CODE_LENGTH = 8


async def _friend_ids(session: AsyncSession, user_id: int) -> list[int]:
    """Accepted friends of a user, whichever side sent the request."""
    rows = await session.execute(
        select(Friendship).where(
            Friendship.status == "accepted",
            or_(
                Friendship.requester_id == user_id,
                Friendship.addressee_id == user_id,
            ),
        )
    )
    ids = []
    for friendship in rows.scalars():
        ids.append(
            friendship.addressee_id
            if friendship.requester_id == user_id
            else friendship.requester_id
        )
    return ids


# Re-exported for the leaderboard endpoints.
friend_ids = _friend_ids


@router.get("/code", response_model=FriendCodeOut)
async def my_friend_code(session: DbSession, user: CurrentUser) -> FriendCodeOut:
    """Shareable code, created on first request.

    Codes exist so that friend requests need a deliberately shared secret —
    an email-lookup endpoint would be an account-enumeration oracle.
    """
    existing = await session.get(FriendCode, user.id)
    if existing is not None:
        return FriendCodeOut(code=existing.code)

    for _ in range(5):
        code = "".join(secrets.choice(_CODE_ALPHABET) for _ in range(_CODE_LENGTH))
        session.add(FriendCode(user_id=user.id, code=code))
        try:
            await session.commit()
            return FriendCodeOut(code=code)
        except IntegrityError:
            await session.rollback()
            # Either the code collided (retry) or a concurrent request already
            # created ours (return theirs).
            existing = await session.get(FriendCode, user.id)
            if existing is not None:
                return FriendCodeOut(code=existing.code)
    raise HTTPException(  # pragma: no cover - 5 collisions in 36^8 space
        status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
        detail="Could not generate a friend code",
    )


@router.post("", response_model=FriendOut, status_code=201)
async def send_request(
    body: FriendRequestIn, session: DbSession, user: CurrentUser
) -> FriendOut:
    code_row = (
        await session.execute(
            select(FriendCode).where(FriendCode.code == body.code.strip().upper())
        )
    ).scalar_one_or_none()
    if code_row is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Unknown friend code"
        )
    if code_row.user_id == user.id:
        raise HTTPException(
            status_code=status.HTTP_422_UNPROCESSABLE_ENTITY,
            detail="That is your own friend code",
        )

    # One row per pair regardless of direction.
    existing = (
        await session.execute(
            select(Friendship).where(
                or_(
                    (Friendship.requester_id == user.id)
                    & (Friendship.addressee_id == code_row.user_id),
                    (Friendship.requester_id == code_row.user_id)
                    & (Friendship.addressee_id == user.id),
                )
            )
        )
    ).scalar_one_or_none()
    if existing is not None:
        if existing.status == "accepted":
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT, detail="Already friends"
            )
        if existing.requester_id == user.id:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT, detail="Request already sent"
            )
        # They asked us first — sending a request back is an acceptance.
        existing.status = "accepted"
        existing.responded_at = datetime.now(UTC)
        await session.commit()
        return await _friend_out(session, existing, user.id)

    friendship = Friendship(requester_id=user.id, addressee_id=code_row.user_id)
    session.add(friendship)
    await session.commit()
    return await _friend_out(session, friendship, user.id)


@router.get("", response_model=list[FriendOut])
async def list_friends(session: DbSession, user: CurrentUser) -> list[FriendOut]:
    rows = (
        (
            await session.execute(
                select(Friendship)
                .where(
                    or_(
                        Friendship.requester_id == user.id,
                        Friendship.addressee_id == user.id,
                    )
                )
                .options(
                    selectinload(Friendship.requester),
                    selectinload(Friendship.addressee),
                )
                .order_by(Friendship.status, Friendship.id.desc())
            )
        )
        .scalars()
        .all()
    )
    return [await _friend_out(session, f, user.id, preloaded=True) for f in rows]


@router.post("/{friendship_id}/accept", response_model=FriendOut)
async def accept(
    friendship_id: int, session: DbSession, user: CurrentUser
) -> FriendOut:
    friendship = await session.get(Friendship, friendship_id)
    # Only the addressee of a pending request may accept it, and anyone else's
    # request is indistinguishable from a missing one.
    if (
        friendship is None
        or friendship.addressee_id != user.id
        or friendship.status != "pending"
    ):
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Request not found"
        )
    friendship.status = "accepted"
    friendship.responded_at = datetime.now(UTC)
    await session.commit()
    return await _friend_out(session, friendship, user.id)


@router.delete("/{friendship_id}", status_code=status.HTTP_204_NO_CONTENT)
async def remove(
    friendship_id: int, session: DbSession, user: CurrentUser
) -> Response:
    """Decline a pending request or end a friendship. Either side may."""
    friendship = await session.get(Friendship, friendship_id)
    if friendship is None or user.id not in (
        friendship.requester_id,
        friendship.addressee_id,
    ):
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="Request not found"
        )
    await session.delete(friendship)
    await session.commit()
    return Response(status_code=status.HTTP_204_NO_CONTENT)


async def _friend_out(
    session: AsyncSession, friendship: Friendship, viewer_id: int, preloaded: bool = False
) -> FriendOut:
    if not preloaded:
        await session.refresh(friendship, ["requester", "addressee"])
    other = (
        friendship.addressee
        if friendship.requester_id == viewer_id
        else friendship.requester
    )
    return FriendOut(
        id=friendship.id,
        display_name=other.display_name,
        status=friendship.status,
        direction="outgoing" if friendship.requester_id == viewer_id else "incoming",
    )
