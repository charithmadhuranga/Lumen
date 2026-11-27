"""Fixed-window counters in Redis (shared by every API replica).

Only failures are counted for sign-in (`hit` after a failed attempt, `blocked`
before trying), so a user who types the password right is never slowed down
by their own successful sign-ins.
"""

from __future__ import annotations

from redis.asyncio import Redis

PREFIX = "rl:"


class RateLimiter:
    def __init__(self, redis: Redis) -> None:
        self.redis = redis

    async def hit(self, key: str, window_s: int) -> int:
        """Count one event; returns the count in the current window."""
        k = PREFIX + key
        async with self.redis.pipeline(transaction=True) as pipe:
            pipe.incr(k)
            pipe.expire(k, window_s, nx=True)
            count, _ = await pipe.execute()
        return int(count)

    async def blocked(self, key: str, limit: int) -> int:
        """0 when allowed; otherwise the seconds until the window resets (>= 1)."""
        k = PREFIX + key
        raw = await self.redis.get(k)
        if raw is None or int(raw) < limit:
            return 0
        ttl = await self.redis.ttl(k)
        return max(int(ttl), 1)

    async def clear(self, key: str) -> None:
        await self.redis.delete(PREFIX + key)
