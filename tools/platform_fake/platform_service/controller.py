from __future__ import annotations

import json
import math
import os
from datetime import datetime, time, timedelta, timezone
from typing import Any

from platform_service.storage import DynamoStore, key

DAILY_LIMIT = 50_000
ROUTES = {
    "POST /v1/reports": (1.0, 2),
    "POST /v1/results": (2.0, 4),
    "POST /v1/qm/tickets": (2.0, 4),
    "GET /v1/qm/tickets/{ticket}": (5.0, 10),
    "DELETE /v1/qm/tickets/{ticket}": (2.0, 4),
    "GET /v1/stats/global": (5.0, 10),
    "GET /v1/stats/me": (5.0, 10),
    "GET /v1/tournaments": (5.0, 10),
    "GET /v1/tournaments/{id}": (5.0, 10),
    "POST /v1/tournaments/{id}/register": (2.0, 4),
    "DELETE /v1/tournaments/{id}/register": (2.0, 4),
    "POST /v1/tournaments/{id}/matches/{mid}/checkin": (2.0, 4),
    "POST /v1/tournaments/{id}/matches/{mid}/result": (2.0, 4),
    "POST /v1/admin/tournaments": (2.0, 4),
    "POST /v1/admin/tournaments/{id}": (2.0, 4),
    "POST /v1/admin/tournaments/{id}/cancel": (2.0, 4),
    "POST /v1/admin/tournaments/{id}/matches/{mid}/resolve": (2.0, 4),
    "GET /v1/admin/metrics": (5.0, 10),
    "$default": (10.0, 20),
}


class ControllerFailure(Exception):
    pass


def _default_clients():
    import boto3

    region = os.environ.get("AWS_REGION", "us-east-2")
    return {
        "cloudwatch": boto3.client("cloudwatch", region_name=region),
        "lambda": boto3.client("lambda", region_name=region),
        "apigatewayv2": boto3.client("apigatewayv2", region_name=region),
    }


def _source_kind(event: Any, *, now: datetime, topic_arn: str) -> str | None:
    if isinstance(event, dict) and event.get("source") == "aws.events":
        return "schedule"
    records = event.get("Records") if isinstance(event, dict) else None
    if not isinstance(records, list):
        return None
    for record in records:
        sns = record.get("Sns") if isinstance(record, dict) else None
        if not isinstance(sns, dict) or sns.get("TopicArn") != topic_arn:
            continue
        try:
            message = json.loads(sns.get("Message", ""))
        except (TypeError, json.JSONDecodeError):
            continue
        if message.get("AlarmName") not in ("platform-daily-total-limit", "platform-controller-errors"):
            continue
        if message.get("NewStateValue") != "ALARM":
            continue
        try:
            changed = datetime.fromisoformat(message["StateChangeTime"].replace("Z", "+00:00"))
        except (KeyError, TypeError, ValueError):
            continue
        if changed.tzinfo is None or abs((now - changed.astimezone(timezone.utc)).total_seconds()) > 600:
            continue
        return "alarm"
    return None


def _day(now: datetime) -> tuple[str, datetime]:
    current = now.astimezone(timezone.utc)
    start = datetime.combine(current.date(), time.min, tzinfo=timezone.utc)
    return current.strftime("%Y-%m-%d"), start


def _daily_count(cloudwatch, gateway, api_id: str, start: datetime, end: datetime) -> float:
    stage = os.environ.get("API_STAGE", "$default")
    if gateway.get_stage(ApiId=api_id, StageName=stage).get("StageName") != stage:
        raise ControllerFailure("controller_stage_missing")
    end = end.replace(second=0, microsecond=0)
    if end == start:
        return 0.0  # No completed minute in this UTC day yet.
    result = cloudwatch.get_metric_data(
        MetricDataQueries=[
            {"Id": "requests", "ReturnData": False, "MetricStat": {
                "Metric": {"Namespace": "AWS/ApiGateway", "MetricName": "Count", "Dimensions": [
                    {"Name": "ApiId", "Value": api_id}, {"Name": "Stage", "Value": stage},
                ]}, "Period": 60, "Stat": "Sum",
            }},
            {"Id": "observed", "ReturnData": True, "Period": 60,
             "Expression": "TIME_SERIES(0) + FILL(requests, 0)"},
        ],
        StartTime=start, EndTime=end, ScanBy="TimestampAscending", MaxDatapoints=2000,
    )
    results = result.get("MetricDataResults", [])
    if result.get("NextToken") or result.get("Messages") or len(results) != 1:
        raise ControllerFailure("controller_metrics_incomplete")
    observed = results[0]
    timestamps, values = observed.get("Timestamps", []), observed.get("Values", [])
    minutes = int((end - start).total_seconds() // 60)
    expected = [start + timedelta(minutes=i) for i in range(minutes)]
    if (observed.get("Id") != "observed" or observed.get("StatusCode") != "Complete"
            or observed.get("Messages") or timestamps != expected or len(values) != minutes):
        raise ControllerFailure("controller_metrics_incomplete")
    if any(isinstance(value, bool) or not isinstance(value, (float, int))
           or not math.isfinite(value) or value < 0 for value in values):
        raise ControllerFailure("controller_metrics_invalid")
    count = sum(values)
    if not math.isfinite(count):
        raise ControllerFailure("controller_metrics_invalid")
    return count


def _publish(cloudwatch, *, now: datetime, api_id: str, count: float) -> None:
    cloudwatch.put_metric_data(
        Namespace="OpenBFME/Platform",
        MetricData=[{
            "MetricName": "DailyRequests",
            "Dimensions": [
                {"Name": "ApiId", "Value": api_id},
                {"Name": "Stage", "Value": os.environ.get("API_STAGE", "$default")},
            ],
            "Timestamp": now,
            "Value": count,
            "Unit": "Count",
        }],
    )


def _route_settings(enabled: bool) -> tuple[dict[str, dict[str, float | int]], dict[str, float | int]]:
    routes = {}
    for route, (rate, burst) in ROUTES.items():
        if route == "$default":
            continue
        routes[route] = {
            "ThrottlingRateLimit": rate if enabled else 0.0,
            "ThrottlingBurstLimit": burst if enabled else 0,
        }
    rate, burst = ROUTES["$default"]
    default = {
        "ThrottlingRateLimit": rate if enabled else 0.0,
        "ThrottlingBurstLimit": burst if enabled else 0,
    }
    return routes, default


def _set_controls(clients: dict[str, Any], *, enabled: bool) -> bool:
    errors = False
    api_id = os.environ.get("API_ID", "")
    stage = os.environ.get("API_STAGE", "$default")
    platform_function = os.environ.get("PLATFORM_FUNCTION_NAME", "")
    crash_function = os.environ.get("CRASH_FUNCTION_NAME", "")
    route_settings, default = _route_settings(enabled)
    try:
        clients["apigatewayv2"].update_stage(
            ApiId=api_id, StageName=stage,
            RouteSettings=route_settings, DefaultRouteSettings=default,
        )
    except Exception:
        errors = True
    for name, concurrency in ((platform_function, 4), (crash_function, 2)):
        try:
            if not name:
                raise ValueError("missing_function")
            clients["lambda"].put_function_concurrency(
                FunctionName=name, ReservedConcurrentExecutions=concurrency if enabled else 0,
            )
        except Exception:
            errors = True
    return errors


def _get_state(store) -> dict[str, Any] | None:
    row = store.get("CONTROL#global", "STATE")
    return row["payload"] if row else None


def _save_state(store, *, cutoff_day: str, tripped: bool, reopened_day: str | None = None) -> None:
    payload = {"cutoff_day": cutoff_day, "tripped": tripped}
    if reopened_day:
        payload["reopened_day"] = reopened_day
    for _ in range(3):
        row = store.get("CONTROL#global", "STATE")
        if store.transact(
            {key("CONTROL#global", "STATE"): row["version"] if row else None},
            [{"pk": "CONTROL#global", "sk": "STATE", "payload": payload}],
        ):
            return
    raise ControllerFailure("controller_state_failure")


def _fail_closed(clients, store, *, day: str) -> None:
    try:
        _set_controls(clients, enabled=False)
    finally:
        _save_state(store, cutoff_day=day, tripped=True)


def _trip(clients, store, day: str) -> dict[str, str]:
    control_error = _set_controls(clients, enabled=False)
    _save_state(store, cutoff_day=day, tripped=True)
    if control_error:
        raise ControllerFailure("controller_control_failure")
    return {"action": "tripped"}


def _process(event, *, now: datetime, clients, store, topic_arn: str) -> dict[str, str]:
    kind = _source_kind(event, now=now, topic_arn=topic_arn)
    if kind is None:
        return {"action": "ignored"}
    api_id = os.environ.get("API_ID", "")
    if not api_id:
        raise ControllerFailure("controller_configuration")
    day, start = _day(now)
    state = _get_state(store)
    if state and state.get("tripped") and state.get("cutoff_day") == day:
        if _set_controls(clients, enabled=False):
            raise ControllerFailure("controller_control_failure")
        return {"action": "held"}
    try:
        count = _daily_count(clients["cloudwatch"], clients["apigatewayv2"], api_id, start, now)
        _publish(clients["cloudwatch"], now=now, api_id=api_id, count=count)
    except Exception:
        _fail_closed(clients, store, day=day)
        raise ControllerFailure("controller_metrics_failure") from None
    if kind == "alarm" or count >= DAILY_LIMIT:
        return _trip(clients, store, day)
    if state and state.get("tripped"):
        if now < start + timedelta(minutes=5):
            _set_controls(clients, enabled=False)
            return {"action": "held"}
        if _set_controls(clients, enabled=True):
            _fail_closed(clients, store, day=day)
            raise ControllerFailure("controller_reopen_failure")
        _save_state(store, cutoff_day=state["cutoff_day"], tripped=False, reopened_day=day)
        return {"action": "open"}
    return {"action": "open"}


def lambda_handler(event, context, *, now: int | None = None, clients=None, store=None) -> dict[str, str]:
    try:
        current = datetime.fromtimestamp(int(now), tz=timezone.utc) if now is not None else datetime.now(timezone.utc)
        control_table = os.environ.get("LIMITER_TABLE", "")
        topic_arn = os.environ.get("ALARM_TOPIC_ARN", "")
        if not control_table or not topic_arn:
            raise ControllerFailure("controller_configuration")
        active_store = store or DynamoStore(control_table, region=os.environ.get("AWS_REGION", "us-east-2"))
        return _process(
            event, now=current, clients=clients or _default_clients(), store=active_store, topic_arn=topic_arn
        )
    except ControllerFailure:
        raise
    except Exception:
        raise ControllerFailure("controller_failure") from None
