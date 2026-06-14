#!/usr/bin/env python3
"""Build a connection graph from a watchlist CSV using Sentry API endpoints.

Usage: tools/extend_watchlist.py --watchlist ../data/watchlist.csv --output out.json \
    --base-url https://sentry.wildknightsquadron.com --token <TOKEN>

The script:
- reads the watchlist CSV (expects a `Handle` column)
- queries `GET {base_url}/api/v1/citizens/{citizen}` and `GET {base_url}/api/v1/orgs/{org}`
- builds a graph of citizens and connections, assigning risk levels by distance:
  - distance 0 (watchlist): level 1
  - distance 1: level 2
  - distance 2: level 3
  - distance >=3: level 4

Outputs a JSON file with nodes and edges.
"""

from __future__ import annotations

import argparse
import csv
import json
import time
import os
from collections import deque
from typing import Dict, Any, List, Set, Tuple, Optional
from urllib.parse import quote

import requests
try:
    import msvcrt
except Exception:
    msvcrt = None

DEFAULT_BASE_URL = "https://sentry.wildknightsquadron.com"


def read_watchlist(path: str) -> List[Dict[str, str]]:
    rows = []
    with open(path, newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        for r in reader:
            rows.append(r)
    return rows


class APIClient:
    def __init__(self, base_url: str, token: str | None = None, pause: float = 0.2):
        self.base_url = base_url.rstrip("/")
        self.session = requests.Session()
        if token:
            self.session.headers.update({"Authorization": f"Bearer {token}"})
        self.pause = pause
        self._cache: Dict[Tuple[str, str], Any] = {}

    def _get(self, path: str):
        key = ("GET", path)
        if key in self._cache:
            print(f"[api] cache hit: {path}")
            return self._cache[key]
        url = f"{self.base_url}{path}"
        print(f"[api] fetching: {url}")
        try:
            resp = self.session.get(url, timeout=10)
            if resp.status_code == 404:
                self._cache[key] = None
                return None
            resp.raise_for_status()
            data = resp.json()
        except Exception:
            print(f"[api] error fetching {url}")
            data = None
        time.sleep(self.pause)
        self._cache[key] = data
        return data

    def get_citizen(self, handle: str):
        return self._get(f"/api/v1/citizens/{quote(handle, safe='')}")

    def get_org(self, org_id: str):
        return self._get(f"/api/v1/orgs/{quote(org_id, safe='')}")


def extract_connections_from_citizen(j: dict) -> List[str]:
    if not j:
        return []
    # new API shape may nest data under "profile" or have explicit keys
    if "profile" in j and isinstance(j["profile"], dict):
        prof = j["profile"]
        for key in ("connections", "associates", "links", "related", "friends"):
            if key in prof and isinstance(prof[key], list):
                return _flatten_handles(prof[key])
    for key in ("connections", "associates", "links", "related", "friends"):
        if key in j and isinstance(j[key], list):
            return _flatten_handles(j[key])
    # fallback: maybe `relationships` or `edges`
    for key in ("relationships", "edges", "associations"):
        if key in j and isinstance(j[key], list):
            return _flatten_handles(j[key])
    return []


def _flatten_handles(lst: List[Any]) -> List[str]:
    out = []
    for item in lst:
        if isinstance(item, str):
            out.append(item)
        elif isinstance(item, dict):
            for k in ("handle", "id", "name", "display_name"):
                if k in item and isinstance(item[k], str):
                    out.append(item[k])
                    break
    return out


def extract_orgs_from_citizen(j: dict) -> {List[str], str}:
    if not j:
        return [], ""
    # handle new API shape: 'memberships' list of dicts with 'organization_sid'
    if "memberships" in j and isinstance(j["memberships"], list):
        out = []
        main_org = ""

        for m in j["memberships"]:
            if isinstance(m, dict):
                sid = m.get("organization_sid") or m.get("org") or m.get("organization")
                if sid:
                    out.append(str(sid))
                if m.get("is_main") or m.get("primary"):
                    main_org = str(sid)
        return out, main_org
    for key in ("orgs", "organizations", "affiliations", "org_ids"):
        if key in j and isinstance(j[key], list):
            return [str(x) for x in j[key]], ""
    # maybe orgs are nested
    if "membership" in j and isinstance(j["membership"], list):
        return [str(x.get("org")) for x in j["membership"] if isinstance(x, dict) and x.get("org")], ""
    return [], ""


def extract_members_from_org(j: dict) -> {int, List[str]}:
    if not j:
        return (0, [])
    # memberships as a list of dicts (e.g., sample org response)
    if "memberships" in j and isinstance(j["memberships"], list):
        out = []
        for m in j["memberships"]:
            if isinstance(m, dict):
                ch = m.get("citizen_handle") or m.get("handle") or m.get("id")
                if ch:
                    out.append(str(ch))
        return (len(out), out)
    # memberships as a dict with 'members' list and optional 'total'
    if "memberships" in j and isinstance(j["memberships"], dict):
        mb = j["memberships"].get("members")
        if isinstance(mb, list):
            out = []
            for m in mb:
                if isinstance(m, dict):
                    ch = m.get("citizen_handle") or m.get("handle") or m.get("id")
                    if ch:
                        out.append(str(ch))
            total = j["memberships"].get("total", len(out))
            return (total, out)
    for key in ("members", "citizens", "users"):
        if key in j and isinstance(j[key], list):
            return (len(j[key]), _flatten_handles(j[key]))
    return (0, [])


def assign_risk(distance: int) -> int:
    if distance <= 0:
        return 1
    if distance == 1:
        return 2
    if distance == 2:
        return 3
    return 4


def build_graph(watchlist: List[Dict[str, str]], api: APIClient, max_depth: int = 4, private_org: str = "RUINOUS", initial_graph: Optional[Dict[str, Any]] = None):
    seeds = [row.get("Handle") or row.get("handle") for row in watchlist]
    seeds = [s for s in seeds if s]

    nodes: Dict[str, Dict[str, Any]] = {}
    edges: List[Dict[str, str]] = []

    # seed nodes
    for row in watchlist:
        handle = row.get("Handle") or row.get("handle")
        if not handle:
            continue
        disp = row.get("Display Name") or row.get("Display") or handle
        risk = row.get("Risk Level") or row.get("Risk") or ""
        if not risk:
            risk = "org enemy"
        nodes[handle] = {
            "handle": handle,
            "display_name": disp,
            "orgs": [],
            "distance": 0,
            "risk_level": 1,
            "risk_reason": risk,
        }

    # BFS from each seed, but keep global visited to compute min distance to any seed
    visited_distance: Dict[str, int] = {h: 0 for h in nodes}
    q = deque()
    for h in list(nodes.keys()):
        q.append((h, 0, h))  # (current_handle, distance, origin_seed)

    # if resuming from an existing graph, load nodes/edges/org cache
    if initial_graph:
        print(f"[graph] resuming from existing graph: preloaded nodes={len(initial_graph.get('nodes', []))}")
        for n in initial_graph.get("nodes", []):
            h = n.get("handle")
            if not h:
                continue
            nodes[h] = n
            visited_distance[h] = n.get("distance", visited_distance.get(h, 0))
        edges = list(initial_graph.get("edges", []))
        # load org cache if members were saved
        ig_orgs = initial_graph.get("orgs") or []
        for og in ig_orgs:
            org = og.get("org")
            members = og.get("members") or og.get("scanned_members") or og.get("members_scanned") or []
            total = og.get("total", len(members))
            if org:
                org_members_cache[org] = {"total": total, "members": members}

    print(f"[graph] starting BFS with {len(nodes)} seed nodes")
    seen_count = 0
    org_members_cache: Dict[str, List[str]] = {}

    stopped = False
    print("[graph] press 's' then Enter to stop and save progress")
    while q:
        cur, dist, origin = q.popleft()
        seen_count += 1
        if seen_count % 20 == 0:
            print(f"[graph] processed {seen_count} queue items; queue size {len(q)}")
        print(f"[graph] expanding: {cur} (dist={dist})")
        # check for stop key
        if msvcrt and msvcrt.kbhit():
            try:
                ch = msvcrt.getwch()
            except Exception:
                ch = None
            if ch and ch.lower() == "s":
                print("[graph] stop requested by user")
                stopped = True
                break

        if dist >= max_depth:
            continue

        cdata = api.get_citizen(cur)
        if cdata is None:
            print(f"[graph] no data for citizen {cur}")
        # enrich orgs
        orgs, main_org = extract_orgs_from_citizen(cdata)
        if cur in nodes:
            nodes[cur]["orgs"] = orgs
            nodes[cur]["main_org"] = main_org
        else:
            nodes[cur] = {
                "handle": cur,
                "display_name": cdata.get("display_name") if isinstance(cdata, dict) else cur,
                "orgs": orgs,
                "main_org": main_org,
                "distance": visited_distance.get(cur, dist),
                "risk_level": assign_risk(visited_distance.get(cur, dist)),
                "risk_reason": "",
            }

        # direct connections from citizen
        conns = extract_connections_from_citizen(cdata)
        for other in conns:
            edges.append({"source": cur, "target": other, "type": "connection"})
            print(f"[graph] found connection: {cur} -> {other}")
            new_dist = dist + 1
            prev = visited_distance.get(other)
            if prev is None or new_dist < prev:
                visited_distance[other] = new_dist
                # create or update node
                if other not in nodes:
                    nodes[other] = {
                        "handle": other,
                        "display_name": other,
                        "orgs": [],
                        "distance": new_dist,
                        "risk_level": assign_risk(new_dist),
                        "risk_reason": "",
                        "main_org": "",
                    }
                    print(f"[graph] discovered node: {other} (dist={new_dist})")
                else:
                    nodes[other]["distance"] = min(nodes[other].get("distance", new_dist), new_dist)
                    nodes[other]["risk_level"] = assign_risk(nodes[other]["distance"])
                q.append((other, new_dist, origin))

        # also expand via org membership: fetch orgs and add edges to members
        for org in orgs:
            if org == private_org and nodes[cur]["distance"] < 2: 
                nodes[cur]["alert"] = True
            
            print(f"[graph] expanding org {org} for {cur}")
            if org in org_members_cache:
                print(f"[graph] org cache hit: {org}")
                continue
            else:
                orgdata = api.get_org(org)
                if orgdata is None:
                    print(f"[graph] no data for org {org}")
                    continue
                total_members, members = extract_members_from_org(orgdata)
                org_members_cache[org] = {"total": total_members, "members": members}
                for m in members:
                    edges.append({"source": cur, "target": m, "type": "org_member", "org": org})
                    print(f"[graph] org member: {cur} -> {m} via {org}")
                    new_dist = dist + 1
                    prev = visited_distance.get(m)
                    if prev is None or new_dist < prev:
                        visited_distance[m] = new_dist
                        if m not in nodes:
                            nodes[m] = {
                                "handle": m,
                                "display_name": m,
                                "orgs": [org],
                                "distance": new_dist,
                                "risk_level": assign_risk(new_dist),
                                "risk_reason": "",
                                "main_org": main_org,
                            }
                            print(f"[graph] discovered member node: {m} (dist={new_dist})")
                        else:
                            nodes[m]["orgs"] = list(set(nodes[m].get("orgs", []) + [org]))
                            nodes[m]["distance"] = min(nodes[m].get("distance", new_dist), new_dist)
                            nodes[m]["risk_level"] = assign_risk(nodes[m]["distance"])
                        q.append((m, new_dist, origin))

    # final normalization: ensure risk levels are minimal across seeds
    for h, info in nodes.items():
        d = info.get("distance", 9999)
        info["risk_level"] = assign_risk(d)

    print(f"[graph] finished: {len(nodes)} nodes, {len(edges)} edges (stopped={stopped})")
    orgs = []
    for org in org_members_cache:
        ent = org_members_cache[org]
        members = ent.get("members") if isinstance(ent.get("members"), list) else []
        total = ent.get("total", len(members))
        orgs.append({"org": org, "total": total, "members": members})
    result = {"nodes": list(nodes.values()), "edges": edges, "orgs": orgs}
    if stopped:
        result["stopped"] = True
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--watchlist", default=os.path.join(".", "data", "watchlist.csv"))
    p.add_argument("--private-org", default="RUINOUS")
    p.add_argument("--output", default="watchlist_graph.json")
    p.add_argument("--resume", default=None, help="Path to existing graph JSON to resume from")
    p.add_argument("--base-url", default=DEFAULT_BASE_URL)
    p.add_argument("--token", default=None)
    p.add_argument("--max-depth", type=int, default=4, help="Max BFS depth (>=3 collapses to level 4)")
    args = p.parse_args()

    wl = read_watchlist(args.watchlist)
    api = APIClient(args.base_url, args.token)
    initial_graph = None
    if args.resume:
        try:
            with open(args.resume, "r", encoding="utf-8") as fh:
                initial_graph = json.load(fh)
            print(f"[main] loaded resume graph from {args.resume}")
        except Exception as e:
            print(f"[main] failed to load resume file {args.resume}: {e}")

    graph = build_graph(wl, api, max_depth=args.max_depth, private_org=args.private_org, initial_graph=initial_graph)

    with open(args.output, "w", encoding="utf-8") as fh:
        json.dump(graph, fh, indent=2, ensure_ascii=False)
    print(f"Wrote graph to {args.output}")


if __name__ == "__main__":
    main()

