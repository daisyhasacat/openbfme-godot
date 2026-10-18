"""Refresh cached global projections using bounded UTC-day counter partitions."""
from __future__ import annotations

from .stats import DAY, global_view, merge_counters, row_counters


def aggregate(store, now: int) -> None:
    # Day windows include today (UTC); all is lifetime anonymous counters.
    today = now // DAY
    by_mode = {}
    for mode in ("faithful", "uber"):
        daily = []
        for day in range(today - 29, today + 1):
            daily.append(merge_counters(row_counters(row) for row in store.iter_query(f"STATS#{mode}#{day}")))
        all_time = merge_counters(row_counters(row) for row in store.iter_query(f"STATS#{mode}#all"))
        by_mode[mode] = {"7d": merge_counters(daily[-7:]), "30d": merge_counters(daily), "all": all_time}
    for mode in ("faithful", "uber", ""):
        for period in ("7d", "30d", "all"):
            counts = (by_mode[mode][period] if mode else
                      merge_counters([by_mode["faithful"][period], by_mode["uber"][period]]))
            store.put("CACHE", f"{mode}#{period}", {"body": global_view(counts, now), "expires_at": now + 7200})
