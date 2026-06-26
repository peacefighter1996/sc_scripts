#!/usr/bin/env python3
"""Query a watchlist graph JSON and export node rows as CSV similar to watchlist.csv.

Usage:
  python tools/query_watchlist_graph.py --graph watchlist_graph.json --output exported.csv

Options:
  --graph    Path to graph JSON (required)
  --output   CSV output path (default: exported_watchlist.csv)
  --min-risk Include nodes with risk_level <= MIN_RISK (1 is highest risk). Default 4 (all)
  --org       Filter nodes that belong to a specific org SID (can be repeated)
  --handle    Filter by handle substring (case-insensitive)
"""

from __future__ import annotations

import argparse
import json
import csv
from typing import List
import os


DEFAULT_HEADERS = [
    "Handle",
    "Display Name",
    "Reason",
    "Org Affiliation",
    "Main Org",
    "Last Seen / Incident Date",
    "Risk Level",
    "Notes",
    "Source",
    "Alert"
    
]


def load_graph(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def node_to_row(node: dict) -> dict:
    handle = node.get("handle")
    display = node.get("display_name") or ""
    reason = node.get("risk_reason") or ""
    orgs = node.get("orgs") or []
    if isinstance(orgs, list):
        org_aff = ";".join(orgs)
    else:
        org_aff = str(orgs)
    risk = node.get("risk_level") or ""
    notes = "ALERT" if node.get("alert") else ""
    src = node.get("source") or "graph"
    return {
        "Handle": handle,
        "Display Name": display,
        "Reason": reason,
        "Org Affiliation": org_aff,
        "Main Org": node.get("main_org") or ""  ,
        "Last Seen / Incident Date": "",
        "Risk Level": risk,
        "Notes": notes,
        "Source": src,
        "Alert": "ALERT" if node.get("alert") else ""
    }


def filter_nodes(nodes: List[dict], min_risk: int = 4, org_filters: List[str] | None = None, handle_sub: str | None = None):
    out = []
    for n in nodes:
        rl = n.get("risk_level") or 4
        if rl > min_risk:
            continue
        if org_filters:
            node_orgs = n.get("orgs") or []
            if not any(o in node_orgs for o in org_filters):
                continue
        if handle_sub:
            if handle_sub.lower() not in (n.get("handle") or "").lower():
                continue
        out.append(n)
    return out


def write_csv(path: str, rows: List[dict]):
    with open(path, "w", encoding="utf-8", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=DEFAULT_HEADERS)
        w.writeheader()
        for r in rows:
            w.writerow(r)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--graph", default="watchlist_graph.json", help="Path to watchlist graph JSON")
    p.add_argument("--output", default="exported_watchlist.csv", help="CSV output path")
    p.add_argument("--min-risk", type=int, default=4, help="Max numeric risk level to include (1..4). 1 highest risk")
    p.add_argument("--org", action="append", help="Org SID to filter by (can repeat)")
    p.add_argument("--handle", help="Handle substring filter (case-insensitive)")
    args = p.parse_args()

    g = load_graph(args.graph)
    nodes = g.get("nodes", [])
    print(f"Loaded graph {args.graph}: {len(nodes)} nodes")
    filtered = filter_nodes(nodes, min_risk=args.min_risk, org_filters=args.org, handle_sub=args.handle)
    print(f"Filtered nodes: {len(filtered)} -> writing {args.output}")
    rows = [node_to_row(n) for n in filtered]
    write_csv(args.output, rows)
    print("Done")


if __name__ == "__main__":
    main()
