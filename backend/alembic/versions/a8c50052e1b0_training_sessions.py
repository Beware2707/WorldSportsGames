"""training sessions

Revision ID: a8c50052e1b0
Revises: a4e7d8ba597a
Create Date: 2026-08-16

The only path that raises a career attribute. Append-only, including
rejected sessions: a silently discarded session is indistinguishable from a
bug, and the trail is what makes grinding patterns visible.
"""
from alembic import op
import sqlalchemy as sa


revision = 'a8c50052e1b0'
down_revision = 'a4e7d8ba597a'
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.create_table(
        'training_session',
        sa.Column('id', sa.Integer(), nullable=False),
        sa.Column('career_athlete_id', sa.Integer(), nullable=False),
        sa.Column('drill', sa.String(length=48), nullable=False),
        sa.Column('attribute_key', sa.String(length=32), nullable=False),
        sa.Column('metric', sa.Float(), nullable=False),
        sa.Column('quality', sa.Float(), nullable=False, server_default='0'),
        sa.Column('attribute_before', sa.Float(), nullable=False, server_default='0'),
        sa.Column('attribute_gain', sa.Float(), nullable=False, server_default='0'),
        sa.Column('xp_awarded', sa.Integer(), nullable=False, server_default='0'),
        sa.Column('accepted', sa.Boolean(), nullable=False, server_default=sa.true()),
        sa.Column('rejection_reason', sa.String(length=200), nullable=True),
        sa.Column('client_ref', sa.String(length=64), nullable=True),
        sa.Column('created_at', sa.DateTime(timezone=True),
                  server_default=sa.text('now()'), nullable=False),
        sa.ForeignKeyConstraint(['career_athlete_id'], ['career_athlete.id']),
        sa.PrimaryKeyConstraint('id'),
    )
    op.create_index('ix_training_session_career_athlete_id', 'training_session',
                    ['career_athlete_id'])
    op.create_index('ix_training_session_accepted', 'training_session', ['accepted'])
    op.create_index('ix_training_athlete_time', 'training_session',
                    ['career_athlete_id', 'created_at'])
    op.create_index(
        'uq_training_client_ref', 'training_session',
        ['career_athlete_id', 'client_ref'], unique=True,
        postgresql_where=sa.text('client_ref IS NOT NULL'),
    )


def downgrade() -> None:
    op.drop_index('uq_training_client_ref', table_name='training_session')
    op.drop_index('ix_training_athlete_time', table_name='training_session')
    op.drop_index('ix_training_session_accepted', table_name='training_session')
    op.drop_index('ix_training_session_career_athlete_id', table_name='training_session')
    op.drop_table('training_session')
