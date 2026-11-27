"""devices and claims

Revision ID: 0003
Revises: 0002
Create Date: 2026-11-27 11:52:54.922069+00:00
"""

from collections.abc import Sequence

import sqlalchemy as sa
from alembic import op

revision: str = "0003"
down_revision: str | None = "0002"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.create_table(
        "devices",
        sa.Column("kind", sa.Enum("gateway", "controller", name="device_kind"), nullable=False),
        sa.Column("home_id", sa.UUID(), nullable=False),
        sa.Column("room_id", sa.UUID(), nullable=True),
        sa.Column("name", sa.String(length=64), nullable=False),
        sa.Column("hw_model", sa.String(length=48), nullable=False),
        sa.Column("board", sa.String(length=48), nullable=True),
        sa.Column("mac", sa.String(length=17), nullable=False),
        sa.Column("chip", sa.String(length=32), nullable=True),
        sa.Column("fw_version", sa.String(length=32), nullable=False),
        sa.Column("controller_id", sa.String(length=32), nullable=True),
        sa.Column("lan_url", sa.String(length=64), nullable=True),
        sa.Column("gateway_id", sa.UUID(), nullable=True),
        sa.Column("route_epoch", sa.Integer(), server_default="0", nullable=False),
        sa.Column("ticket_epoch", sa.Integer(), server_default="0", nullable=False),
        sa.Column("local_key_enc", sa.LargeBinary(length=64), nullable=True),
        sa.Column("local_key_kid", sa.String(length=16), nullable=True),
        sa.Column("last_seen_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("id", sa.UUID(), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.CheckConstraint(
            "(kind = 'gateway' AND gateway_id IS NULL AND controller_id IS NULL) OR (kind = 'controller' AND local_key_enc IS NOT NULL)",
            name=op.f("ck_devices_kind_fields"),
        ),
        sa.CheckConstraint("route_epoch >= 0 AND ticket_epoch >= 0", name=op.f("ck_devices_epochs")),
        sa.ForeignKeyConstraint(
            ["gateway_id"], ["devices.id"], name=op.f("fk_devices_gateway_id_devices"), ondelete="SET NULL"
        ),
        sa.ForeignKeyConstraint(["home_id"], ["homes.id"], name=op.f("fk_devices_home_id_homes"), ondelete="CASCADE"),
        sa.ForeignKeyConstraint(["room_id"], ["rooms.id"], name=op.f("fk_devices_room_id_rooms"), ondelete="SET NULL"),
        sa.PrimaryKeyConstraint("id", name=op.f("pk_devices")),
    )
    op.create_index(op.f("ix_devices_gateway_id"), "devices", ["gateway_id"], unique=False)
    op.create_index(op.f("ix_devices_home_id"), "devices", ["home_id"], unique=False)
    op.create_index("ix_devices_home_mac", "devices", ["home_id", "mac"], unique=False)
    op.create_table(
        "claims",
        sa.Column("token_hash", sa.LargeBinary(length=32), nullable=False),
        sa.Column("kind", sa.Enum("gateway", "controller", name="device_kind"), nullable=False),
        sa.Column("user_id", sa.UUID(), nullable=False),
        sa.Column("home_id", sa.UUID(), nullable=False),
        sa.Column("gateway_id", sa.UUID(), nullable=True),
        sa.Column("setup_proof_hash", sa.LargeBinary(length=32), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.Column("expires_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("used_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("device_id", sa.UUID(), nullable=True),
        sa.Column("id", sa.UUID(), nullable=False),
        sa.CheckConstraint(
            "(kind = 'gateway' AND setup_proof_hash IS NOT NULL AND gateway_id IS NULL) OR (kind = 'controller' AND gateway_id IS NOT NULL AND setup_proof_hash IS NULL)",
            name=op.f("ck_claims_kind_fields"),
        ),
        sa.ForeignKeyConstraint(
            ["device_id"], ["devices.id"], name=op.f("fk_claims_device_id_devices"), ondelete="SET NULL"
        ),
        sa.ForeignKeyConstraint(
            ["gateway_id"], ["devices.id"], name=op.f("fk_claims_gateway_id_devices"), ondelete="CASCADE"
        ),
        sa.ForeignKeyConstraint(["home_id"], ["homes.id"], name=op.f("fk_claims_home_id_homes"), ondelete="CASCADE"),
        sa.ForeignKeyConstraint(["user_id"], ["users.id"], name=op.f("fk_claims_user_id_users"), ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id", name=op.f("pk_claims")),
        sa.UniqueConstraint("token_hash", name=op.f("uq_claims_token_hash")),
    )
    op.create_index(op.f("ix_claims_home_id"), "claims", ["home_id"], unique=False)
    op.create_table(
        "gateway_credentials",
        sa.Column("gateway_id", sa.UUID(), nullable=False),
        sa.Column("secret_hash", sa.LargeBinary(length=32), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), server_default=sa.text("now()"), nullable=False),
        sa.Column("revoked_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("id", sa.UUID(), nullable=False),
        sa.ForeignKeyConstraint(
            ["gateway_id"], ["devices.id"], name=op.f("fk_gateway_credentials_gateway_id_devices"), ondelete="CASCADE"
        ),
        sa.PrimaryKeyConstraint("id", name=op.f("pk_gateway_credentials")),
        sa.UniqueConstraint("secret_hash", name=op.f("uq_gateway_credentials_secret_hash")),
    )
    op.create_index(op.f("ix_gateway_credentials_gateway_id"), "gateway_credentials", ["gateway_id"], unique=False)


def downgrade() -> None:
    op.drop_index(op.f("ix_gateway_credentials_gateway_id"), table_name="gateway_credentials")
    op.drop_table("gateway_credentials")
    op.drop_index(op.f("ix_claims_home_id"), table_name="claims")
    op.drop_table("claims")
    op.drop_index("ix_devices_home_mac", table_name="devices")
    op.drop_index(op.f("ix_devices_home_id"), table_name="devices")
    op.drop_index(op.f("ix_devices_gateway_id"), table_name="devices")
    op.drop_table("devices")
    op.execute("DROP TYPE IF EXISTS device_kind")
