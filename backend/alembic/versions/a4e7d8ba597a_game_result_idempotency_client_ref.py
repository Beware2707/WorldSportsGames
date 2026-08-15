"""game result idempotency client_ref

Revision ID: a4e7d8ba597a
Revises: 63c232ae2282
Create Date: 2026-08-16 00:00:00

A replayed submission (client timeout, crash, offline-queue flush) carries
the same client_ref and must be answered with the stored outcome, never
recorded or rewarded twice. was_pb preserves the original "personal best"
answer, which cannot be recomputed later without comparing a result against
itself.
"""
from alembic import op
import sqlalchemy as sa


revision = 'a4e7d8ba597a'
down_revision = '63c232ae2282'
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column(
        'game_result', sa.Column('client_ref', sa.String(length=64), nullable=True)
    )
    op.add_column(
        'game_result',
        sa.Column('was_pb', sa.Boolean(), nullable=False, server_default=sa.false()),
    )
    op.create_index(
        'uq_game_result_client_ref',
        'game_result',
        ['career_athlete_id', 'client_ref'],
        unique=True,
        postgresql_where=sa.text('client_ref IS NOT NULL'),
    )


def downgrade() -> None:
    op.drop_index('uq_game_result_client_ref', table_name='game_result')
    op.drop_column('game_result', 'was_pb')
    op.drop_column('game_result', 'client_ref')
