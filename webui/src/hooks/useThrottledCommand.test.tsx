import { afterEach, describe, expect, it, vi } from "vitest";
import { act, renderHook } from "@testing-library/react";

import { useThrottled } from "@/hooks/useThrottledCommand";

afterEach(() => vi.useRealTimers());

describe("useThrottled (DESIGN §8.2)", () => {
  it("sends at most one value per 100 ms while dragging and the final value on commit", () => {
    vi.useFakeTimers();
    const send = vi.fn();
    const { result } = renderHook(() => useThrottled(send, 100));
    act(() => {
      for (let v = 1; v <= 20; v++) {
        result.current.push(v);
        vi.advanceTimersByTime(10); // 20 values over 200 ms
      }
    });
    expect(send.mock.calls.length).toBeLessThanOrEqual(3);
    act(() => result.current.commit(42));
    expect(send).toHaveBeenLastCalledWith(42);
    act(() => vi.advanceTimersByTime(500));
    expect(send).toHaveBeenLastCalledWith(42); // no stale trailing send after commit
  });

  it("a newer value replaces a pending one", () => {
    vi.useFakeTimers();
    const send = vi.fn();
    const { result } = renderHook(() => useThrottled(send, 100));
    act(() => {
      result.current.push(1); // immediate
      result.current.push(2); // pending
      result.current.push(3); // replaces 2
      vi.advanceTimersByTime(150);
    });
    expect(send.mock.calls.map((c) => c[0])).toEqual([1, 3]);
  });
});
