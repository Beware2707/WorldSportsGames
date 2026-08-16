"""game session idempotency

Revision ID: 8b20236bfe38
Revises: a8c50052e1b0
Create Date: 2026-08-16

The 2D mini-games awarded XP again for a replayed submission — resending a
captured request needed no modified client at all. Mirrors the career
result fix (a4e7d8ba597a): a client_ref with a partial unique index, plus a
stored personal-best verdict so a replay can be answered with the original
outcome instead of recomputed against itself.
"""
from alembic import op
import sqlalchemy as sa


revision = '8b20236bfe38'
down_revision = 'a8c50052e1b0'
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column('game_session',
                  sa.Column('client_ref', sa.String(length=64), nullable=True))
    op.add_column('game_session',
                  sa.Column('was_personal_best', sa.Boolean(), nullable=False,
                            server_default=sa.false()))
    op.create_index(
        'uq_game_session_client_ref', 'game_session',
        ['user_id', 'game_id', 'client_ref'], unique=True,
        postgresql_where=sa.text('client_ref IS NOT NULL'),
    )


def downgrade() -> None:
    op.drop_index('uq_game_session_client_ref', table_name='game_session')
    op.drop_column('game_session', 'was_personal_best')
    op.drop_column('game_session', 'client_ref')
