"""friendship undirected pair key

Revision ID: 63c232ae2282
Revises: 8dd03448307d
Create Date: 2026-08-15 15:31:45.613451

The directional (requester_id, addressee_id) unique constraint cannot stop
two *reverse* requests racing — A→B and B→A each satisfy it, leaving two
rows for one pair. Replace it with a unique key on the order-independent
pair identity ("min_id:max_id").
"""
from alembic import op
import sqlalchemy as sa


revision = '63c232ae2282'
down_revision = '8dd03448307d'
branch_labels = None
depends_on = None


def upgrade() -> None:
    op.add_column(
        'friendship', sa.Column('pair_key', sa.String(length=32), nullable=True)
    )
    op.execute(
        "UPDATE friendship SET pair_key = "
        "least(requester_id, addressee_id)::text || ':' || "
        "greatest(requester_id, addressee_id)::text"
    )
    # If reverse-request races already left two rows for one pair, keep the
    # accepted one (else the oldest) so the unique constraint can be created.
    op.execute(
        "DELETE FROM friendship WHERE id IN ("
        "  SELECT id FROM ("
        "    SELECT id, row_number() OVER ("
        "      PARTITION BY pair_key"
        "      ORDER BY (status = 'accepted') DESC, id"
        "    ) AS rn FROM friendship"
        "  ) ranked WHERE rn > 1"
        ")"
    )
    op.alter_column('friendship', 'pair_key', nullable=False)
    op.drop_constraint('uq_friendship_pair', 'friendship', type_='unique')
    op.create_unique_constraint('uq_friendship_pair_key', 'friendship', ['pair_key'])


def downgrade() -> None:
    op.drop_constraint('uq_friendship_pair_key', 'friendship', type_='unique')
    op.create_unique_constraint(
        'uq_friendship_pair', 'friendship', ['requester_id', 'addressee_id']
    )
    op.drop_column('friendship', 'pair_key')
