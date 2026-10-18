"""Daily install cardinalities. No raw install or source address is stored here."""
from __future__ import annotations

import json
from datetime import datetime, timezone

from .identity import daily_install_hash
from .protocol import RequestError, client_metadata, normalize_install

DAY = 86400
VERSION_CAP = 16
VERSION_REGISTRY = "!VERSIONS"


def dimension(kind: str, *values: str) -> str:
    return json.dumps([kind, *values], separators=(",", ":"), ensure_ascii=True)


class Metrics:
    def __init__(self, store, server_key: bytes):
        self.store = store
        self.server_key = server_key

    def prepare(self, install: str, client: dict, now: int, *, crash: bool = False):
        """CAS plan; crash admissions append this to their nonce reservation transaction."""
        install = normalize_install(install)
        client = client_metadata(client)
        day = now // DAY
        digest = daily_install_hash(self.server_key, day, install)
        expected, writes = {}, []
        registry_key = (f"METRICS#{day}", VERSION_REGISTRY)
        registry = self.store.get(*registry_key)
        versions = list(registry["payload"]["versions"]) if registry else []
        expected[registry_key] = registry["version"] if registry else None
        version = client["version"]
        if version not in versions and len(versions) < VERSION_CAP:
            versions.append(version)
            writes.append({"pk": registry_key[0], "sk": registry_key[1],
                           "payload": {"versions": versions}, "expires_at": (day + 366) * DAY})
        overflow = version not in versions
        dimensions = [dimension("all"), dimension("platform", client["platform"]),
                      dimension("version_overflow") if overflow else dimension("version", version),
                      dimension("client_overflow", client["platform"]) if overflow else
                      dimension("client", client["platform"], version)]
        for dim in dimensions:
            seen_key = (f"METRICSEEN#{day}", f"{dim}#{digest}")
            seen = self.store.get(*seen_key)
            new_install = seen is None or int(seen.get("expires_at", 0)) <= now
            crash_increment = int(crash and dim == dimensions[-1])
            if not new_install and not crash_increment:
                continue
            count_key = (f"METRICS#{day}", dim)
            count = self.store.get(*count_key)
            payload = dict(count["payload"]) if count else {"installs": 0, "crash_reports": 0}
            payload["installs"] += int(new_install)
            payload["crash_reports"] += crash_increment
            expected[count_key] = count["version"] if count else None
            writes.append({"pk": count_key[0], "sk": count_key[1], "payload": payload,
                           "expires_at": (day + 366) * DAY})
            if new_install:
                expected[seen_key] = seen["version"] if seen else None
                writes.append({"pk": seen_key[0], "sk": seen_key[1], "payload": {},
                               "expires_at": now + 2 * DAY})
        return expected, writes

    def transact(self, expected: dict, writes: list, install: str, client: dict, now: int) -> bool:
        metric_expected, metric_writes = self.prepare(install, client, now)
        if expected.keys() & metric_expected.keys():
            raise ValueError("metric_transaction_overlap")
        return self.store.transact({**expected, **metric_expected}, [*writes, *metric_writes])

    def seen(self, install: str, client: dict, now: int) -> None:
        for _ in range(3):
            expected, writes = self.prepare(install, client, now)
            if not writes or self.store.transact(expected, writes):
                return
        raise RuntimeError("metrics_conflict")

    def view(self, platform_store, now: int, days: int = 30) -> dict:
        if type(days) is not int or not 1 <= days <= 30:
            raise RequestError(400, "invalid_field", "days")
        rows = []
        today = now // DAY
        for day in range(today - days + 1, today + 1):
            daily = {"day": datetime.fromtimestamp(day * DAY, timezone.utc).strftime("%Y-%m-%d"),
                     "active_installs": 0, "platforms": [], "versions": [], "clients": [],
                     "crash_reports": 0, "games": {"faithful": 0, "uber": 0}}
            for row in self.store.query(f"METRICS#{day}"):
                if int(row.get("expires_at", 0)) <= now:
                    continue
                if row["sk"] == VERSION_REGISTRY:
                    continue
                dim = json.loads(row["sk"])
                data = row["payload"]
                if dim[0] == "all":
                    daily["active_installs"] = data["installs"]
                elif dim[0] == "platform":
                    daily["platforms"].append({"platform": dim[1], "installs": data["installs"]})
                elif dim[0] == "version":
                    daily["versions"].append({"version": dim[1], "installs": data["installs"]})
                elif dim[0] == "version_overflow":
                    daily["versions"].append({"version": "other", "installs": data["installs"],
                                              "overflow": True})
                elif dim[0] == "client":
                    daily["clients"].append({"platform": dim[1], "version": dim[2], **data})
                    daily["crash_reports"] += data["crash_reports"]
                elif dim[0] == "client_overflow":
                    daily["clients"].append({"platform": dim[1], "version": "other", **data,
                                            "overflow": True})
                    daily["crash_reports"] += data["crash_reports"]
                else:
                    raise ValueError("unknown_metric_dimension")
            for mode in ("faithful", "uber"):
                for bucket in ("pvp", "vs_ai"):
                    count = platform_store.get(f"STATS#{mode}#{day}", f"{bucket}#total")
                    if count and int(count.get("expires_at", 0)) > now:
                        daily["games"][mode] += count["payload"]["counts"]["games"]
            rows.append(daily)
        return {"v": 1, "as_of": now, "days": rows}
