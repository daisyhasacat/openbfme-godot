from __future__ import annotations

import copy
import heapq
import json
import threading
from typing import Any

MAX_TRANSACTION_ITEMS = 100
MAX_ITEM_BYTES = 400_000
MAX_QUERY_ITEMS = 100


def key(pk: str, sk: str) -> tuple[str, str]:
    return pk, sk


def _wire_item(item: dict[str, Any]) -> dict[str, Any]:
    payload = item.get("payload", {})
    encoded = json.dumps(payload, separators=(",", ":"), ensure_ascii=False, allow_nan=False)
    if len(encoded.encode("utf-8")) > MAX_ITEM_BYTES:
        raise ValueError("item_too_large")
    result = {
        "pk": item["pk"],
        "sk": item["sk"],
        "payload": encoded,
        "version": int(item.get("version", 1)),
    }
    if "expires_at" in item:
        result["expires_at"] = int(item["expires_at"])
    return result


def _row(item: dict[str, Any]) -> dict[str, Any]:
    payload = json.loads(item["payload"]) if isinstance(item.get("payload"), str) else item.get("payload", {})
    row = {"pk": item["pk"], "sk": item["sk"], "payload": payload, "version": int(item.get("version", 1)),
           **({"expires_at": int(item["expires_at"])} if "expires_at" in item else {})}
    if "counts" in payload:
        row["counts"] = payload["counts"]
    return row


def _validate_transaction(
    expected: dict[tuple[str, str], int | None],
    puts: list[dict[str, Any]],
) -> set[tuple[str, str]]:
    put_pairs = {(item["pk"], item["sk"]) for item in puts}
    if len(put_pairs) != len(puts) or any(pair not in expected for pair in put_pairs):
        raise ValueError("invalid_transaction")
    if len(puts) + len(expected.keys() - put_pairs) > MAX_TRANSACTION_ITEMS:
        raise ValueError("invalid_transaction")
    return put_pairs


class PaginatedStore:
    def query(self, pk: str) -> list[dict[str, Any]]:
        rows, cursor = self.query_page(pk)
        if cursor is not None:
            raise ValueError("query_requires_pagination")
        return rows

    def iter_query(self, pk: str):
        cursor = None
        while True:
            rows, cursor = self.query_page(pk, cursor=cursor)
            yield from rows
            if cursor is None:
                return


class MemoryStore(PaginatedStore):
    """Thread-safe test/fake store implementing the platform adapter contract."""

    def __init__(self):
        self._rows: dict[tuple[str, str], dict[str, Any]] = {}
        self._lock = threading.RLock()

    def get(self, pk: str, sk: str) -> dict[str, Any] | None:
        with self._lock:
            item = self._rows.get((pk, sk))
            return copy.deepcopy(item) if item is not None else None

    def query_page(self, pk: str, *, limit: int = MAX_QUERY_ITEMS, cursor: str | None = None,
                   reverse: bool = False) -> tuple[list[dict[str, Any]], str | None]:
        if type(limit) is not int or not 1 <= limit <= MAX_QUERY_ITEMS:
            raise ValueError("invalid_query_limit")
        with self._lock:
            candidates = ((sk, row) for (row_pk, sk), row in self._rows.items()
                          if row_pk == pk and (cursor is None or (sk < cursor if reverse else sk > cursor)))
            select = heapq.nlargest if reverse else heapq.nsmallest
            selected = select(limit + 1, candidates, key=lambda item: item[0])
            rows = [copy.deepcopy(_row(row)) for _, row in selected[:limit]]
            return rows, rows[-1]["sk"] if len(selected) > limit else None

    def transact(self, expected: dict[tuple[str, str], int | None], puts: list[dict[str, Any]]) -> bool:
        _validate_transaction(expected, puts)
        for item in puts:
            _wire_item(item)
        with self._lock:
            for pair, version in expected.items():
                current = self._rows.get(pair)
                if (current is None and version is not None) or (
                    current is not None and current["version"] != version
                ):
                    return False
            if any((item["pk"], item["sk"]) not in expected for item in puts):
                raise ValueError("missing_condition")
            for item in puts:
                pair = (item["pk"], item["sk"])
                old = self._rows.get(pair)
                row = _wire_item(item)
                row["version"] = 1 if old is None else old["version"] + 1
                self._rows[pair] = _row(row)
            return True

    def put(self, pk: str, sk: str, payload: dict[str, Any]) -> None:
        for _ in range(3):
            current = self.get(pk, sk)
            expected = {(pk, sk): current["version"] if current else None}
            item = {"pk": pk, "sk": sk, "payload": payload}
            if self.transact(expected, [item]):
                return
        raise RuntimeError("storage_conflict")


class DynamoStore(PaginatedStore):
    """DynamoDB implementation. boto3 is imported only when this adapter is created."""

    def __init__(self, table_name: str, *, region: str = "us-east-2"):
        import boto3
        from boto3.dynamodb.conditions import Key

        if not table_name:
            raise ValueError("missing_table_name")
        self._boto3 = boto3
        self._key_condition = Key
        self.table = boto3.resource("dynamodb", region_name=region).Table(table_name)
        self.client = boto3.client("dynamodb", region_name=region)

    def get(self, pk: str, sk: str) -> dict[str, Any] | None:
        response = self.table.get_item(Key={"pk": pk, "sk": sk}, ConsistentRead=True)
        item = response.get("Item")
        return _row(item) if item else None

    def query_page(self, pk: str, *, limit: int = MAX_QUERY_ITEMS, cursor: str | None = None,
                   reverse: bool = False) -> tuple[list[dict[str, Any]], str | None]:
        if type(limit) is not int or not 1 <= limit <= MAX_QUERY_ITEMS:
            raise ValueError("invalid_query_limit")
        args = {"KeyConditionExpression": self._key_condition("pk").eq(pk), "ConsistentRead": True,
                "Limit": limit, "ScanIndexForward": not reverse}
        if cursor is not None:
            args["ExclusiveStartKey"] = {"pk": pk, "sk": cursor}
        result = self.table.query(**args)
        rows = [_row(item) for item in result.get("Items", [])]
        return rows, result.get("LastEvaluatedKey", {}).get("sk")

    def transaction_items(self, expected: dict[tuple[str, str], int | None], puts: list[dict[str, Any]]) -> list:
        from boto3.dynamodb.types import TypeSerializer

        put_pairs = _validate_transaction(expected, puts)
        serializer = TypeSerializer()
        table_name = self.table.name
        operations = []
        for item in puts:
            pair = (item["pk"], item["sk"])
            if pair not in expected:
                raise ValueError("missing_condition")
            old = expected[pair]
            wire = _wire_item(item)
            wire["version"] = 1 if old is None else old + 1
            serialized = {name: serializer.serialize(value) for name, value in wire.items()}
            if old is None:
                condition = "attribute_not_exists(pk) AND attribute_not_exists(sk)"
                values = None
                names = None
            else:
                condition = "#version = :version"
                values = {":version": serializer.serialize(old)}
                names = {"#version": "version"}
            put = {"TableName": table_name, "Item": serialized, "ConditionExpression": condition}
            if values:
                put["ExpressionAttributeValues"] = values
            if names:
                put["ExpressionAttributeNames"] = names
            operations.append({"Put": put})
        for (pk, sk), old in expected.items():
            if (pk, sk) in put_pairs:
                continue
            check = {
                "TableName": table_name,
                "Key": {
                    "pk": serializer.serialize(pk),
                    "sk": serializer.serialize(sk),
                },
            }
            if old is None:
                check["ConditionExpression"] = "attribute_not_exists(pk) AND attribute_not_exists(sk)"
            else:
                check["ConditionExpression"] = "#version = :version"
                check["ExpressionAttributeNames"] = {"#version": "version"}
                check["ExpressionAttributeValues"] = {":version": serializer.serialize(old)}
            operations.append({"ConditionCheck": check})
        return operations

    def transact(self, expected: dict[tuple[str, str], int | None], puts: list[dict[str, Any]]) -> bool:
        from botocore.exceptions import ClientError

        operations = self.transaction_items(expected, puts)
        try:
            self.client.transact_write_items(TransactItems=operations)
            return True
        except ClientError as error:
            if error.response.get("Error", {}).get("Code") in (
                "TransactionCanceledException", "TransactionConflictException",
            ):
                return False
            raise

    def put(self, pk: str, sk: str, payload: dict[str, Any]) -> None:
        for _ in range(3):
            current = self.get(pk, sk)
            expected = {(pk, sk): current["version"] if current else None}
            if self.transact(expected, [{"pk": pk, "sk": sk, "payload": payload}]):
                return
        raise RuntimeError("storage_conflict")
