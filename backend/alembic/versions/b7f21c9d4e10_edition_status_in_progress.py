"""Rename edition status 'live' -> 'in_progress'

An edition spanning days or months is not live coverage. Only a LiveEvent row
may drive a LIVE indicator, so the ambiguous 'live' edition status is retired.

Revision ID: b7f21c9d4e10
Revises: a3d8fa243a4b
Create Date: 2026-08-10
"""
import sqlalchemy as sa
from alembic import op

revision = "b7f21c9d4e10"
down_revision = "a3d8fa243a4b"
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.execute(
        sa.text(
            "UPDATE competition_edition SET status = 'in_progress' "
            "WHERE status = 'live'"
        )
    )


def downgrade() -> None:
    op.execute(
        sa.text(
            "UPDATE competition_edition SET status = 'live' "
            "WHERE status = 'in_progress'"
        )
    )
