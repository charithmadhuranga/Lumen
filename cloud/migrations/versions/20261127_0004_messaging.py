"""messaging

Revision ID: 0004
Revises: 0003
Create Date: 2026-11-27 12:43:31.908769+00:00
"""

from collections.abc import Sequence

import sqlalchemy as sa
from alembic import op
from sqlalchemy.dialects import postgresql

revision: str = "0004"
down_revision: str | None = "0003"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.create_table(
        "commands",
        sa.Column("id", sa.UUID(), nullable=False),
        sa.Column("device_id", sa.UUID(), nullable=False),
        sa.Column("home_id", sa.UUID(), nullable=False),
        sa.Column("user_id", sa.UUID(), nullable=True),
        sa.Column("op", sa.String(length=24), nullable=False),
        sa.Column("target", sa.String(length=48), nullable=True),
        sa.Column("args", postgresql.JSONB(astext_type=sa.Text()), nullable=True),
        sa.Column(
            "status",
            sa.Enum("queued", "sent", "received", "applied", "rejected", "expired", "failed", name="command_status"),
            nullable=False,
        ),
        sa.Column("code", sa.String(length=32), nullable=True),
        sa.Column("message", sa.String(length=160), nullable=True),
        sa.Column("idempotency_key", sa.String(length=64), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.Column("expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("sent_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("completed_at", sa.DateTime(timezone=True), nullable=True),
        sa.ForeignKeyConstraint(
            ["device_id"], ["devices.id"], name=op.f("fk_commands_device_id_devices"), ondelete="CASCADE"
        ),
        sa.ForeignKeyConstraint(["home_id"], ["homes.id"], name=op.f("fk_commands_home_id_homes"), ondelete="CASCADE"),
        sa.ForeignKeyConstraint(["user_id"], ["users.id"], name=op.f("fk_commands_user_id_users"), ondelete="SET NULL"),
        sa.PrimaryKeyConstraint("id", name=op.f("pk_commands")),
        sa.UniqueConstraint("user_id", "idempotency_key", name="uq_commands_user_idempotency"),
    )
    op.create_index("ix_commands_device_created", "commands", ["device_id", "created_at"], unique=False)
    op.create_index(
        "ix_commands_open",
        "commands",
        ["status", "expires_at"],
        unique=False,
        postgresql_where="status IN ('queued','sent','received')",
    )
    op.create_table(
        "device_configs",
        sa.Column("device_id", sa.UUID(), nullable=False),
        sa.Column("rev", sa.Integer(), nullable=False),
        sa.Column("doc", postgresql.JSONB(astext_type=sa.Text()), nullable=False),
        sa.Column("reported_ts", sa.BigInteger(), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.ForeignKeyConstraint(
            ["device_id"], ["devices.id"], name=op.f("fk_device_configs_device_id_devices"), ondelete="CASCADE"
        ),
        sa.PrimaryKeyConstraint("device_id", name=op.f("pk_device_configs")),
    )
    op.create_table(
        "device_events",
        sa.Column("id", sa.BigInteger(), autoincrement=True, nullable=False),
        sa.Column("device_id", sa.UUID(), nullable=False),
        sa.Column("received_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.Column("payload", postgresql.JSONB(astext_type=sa.Text()), nullable=False),
        sa.ForeignKeyConstraint(
            ["device_id"], ["devices.id"], name=op.f("fk_device_events_device_id_devices"), ondelete="CASCADE"
        ),
        sa.PrimaryKeyConstraint("id", name=op.f("pk_device_events")),
    )
    op.create_index("ix_device_events_device_received", "device_events", ["device_id", "received_at"], unique=False)
    op.create_table(
        "device_states",
        sa.Column("device_id", sa.UUID(), nullable=False),
        sa.Column("online", sa.Boolean(), server_default="false", nullable=False),
        sa.Column("reported", postgresql.JSONB(astext_type=sa.Text()), nullable=True),
        sa.Column("reported_ts", sa.BigInteger(), nullable=True),
        sa.Column("telemetry", postgresql.JSONB(astext_type=sa.Text()), nullable=True),
        sa.Column("availability_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("updated_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.ForeignKeyConstraint(
            ["device_id"], ["devices.id"], name=op.f("fk_device_states_device_id_devices"), ondelete="CASCADE"
        ),
        sa.PrimaryKeyConstraint("device_id", name=op.f("pk_device_states")),
    )


def downgrade() -> None:
    op.drop_table("device_states")
    op.drop_index("ix_device_events_device_received", table_name="device_events")
    op.drop_table("device_events")
    op.drop_table("device_configs")
    op.drop_index("ix_commands_open", table_name="commands", postgresql_where="status IN ('queued','sent','received')")
    op.drop_index("ix_commands_device_created", table_name="commands")
    op.drop_table("commands")
    op.execute("DROP TYPE IF EXISTS command_status")
