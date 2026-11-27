"""Home roles and what each may do. The one place that decides it (p02 plan).

Later phases add their actions here (devices, commands, OTA) and every route
asks `allowed(role, action)` through the `require` dependency.
"""

from __future__ import annotations

import enum

from app.models.home import Role


class Action(enum.StrEnum):
    home_read = "home.read"
    home_update = "home.update"
    home_delete = "home.delete"
    home_transfer = "home.transfer"
    members_read = "members.read"
    members_manage = "members.manage"  # change roles, remove others
    rooms_read = "rooms.read"
    rooms_write = "rooms.write"
    control = "control"  # lights, scenes (P4)
    devices_manage = "devices.manage"  # claim, rename, remove, restart (P3/P4)


_MATRIX: dict[Role, frozenset[Action]] = {
    Role.owner: frozenset(Action),
    Role.admin: frozenset(Action) - {Action.home_delete, Action.home_transfer},
    Role.member: frozenset({Action.home_read, Action.members_read, Action.rooms_read, Action.control}),
    Role.guest: frozenset({Action.home_read, Action.rooms_read, Action.control}),
}

# Rank for "may act on a member with this role" checks: you can manage only
# members ranked below you, and only assign roles below your own.
RANK = {Role.owner: 3, Role.admin: 2, Role.member: 1, Role.guest: 0}


def allowed(role: Role, action: Action) -> bool:
    return action in _MATRIX[role]


def matrix() -> dict[str, list[str]]:
    """For docs and tests."""
    return {r.value: sorted(a.value for a in _MATRIX[r]) for r in Role}
