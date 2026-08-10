import os

# Must be set before any app import: settings are cached at first access.
# Empty redis_url = cache + live pub/sub disabled (local-only, deterministic);
# dev fixtures on so the simulator endpoint is testable.
os.environ["SPORTS_REDIS_URL"] = ""
os.environ["SPORTS_DEBUG"] = "true"
os.environ["SPORTS_ENABLE_DEV_FIXTURES"] = "true"

from collections.abc import AsyncIterator  # noqa: E402

import pytest  # noqa: E402
from httpx import ASGITransport, AsyncClient  # noqa: E402
from sqlalchemy.ext.asyncio import (  # noqa: E402
    async_sessionmaker,
    create_async_engine,
)

from app.core.config import get_settings  # noqa: E402
from app.db.base import Base  # noqa: E402
from app.db.session import get_db  # noqa: E402
from app.main import create_app  # noqa: E402
from app.seed.run import seed_dev_fixtures, seed_reference, seed_taxonomy  # noqa: E402

get_settings.cache_clear()


@pytest.fixture
async def db_sessionmaker():
    engine = create_async_engine("sqlite+aiosqlite:///:memory:")
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    factory = async_sessionmaker(engine, expire_on_commit=False)
    async with factory() as session:
        await seed_taxonomy(session)
        await seed_reference(session)
        await seed_dev_fixtures(session)
        await session.commit()
    yield factory
    await engine.dispose()


@pytest.fixture
async def test_app(db_sessionmaker):
    app = create_app()

    async def override_get_db():
        async with db_sessionmaker() as session:
            yield session

    app.dependency_overrides[get_db] = override_get_db
    # Background simulator sessions must also hit the test database.
    app.state.session_factory = db_sessionmaker
    return app


@pytest.fixture
async def client(test_app) -> AsyncIterator[AsyncClient]:
    transport = ASGITransport(app=test_app)
    async with AsyncClient(transport=transport, base_url="http://test") as c:
        yield c
