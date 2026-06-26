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
import threading
import concurrent.futures
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
        self._lock = threading.Lock()
        # executor for parallel requests; number of workers can be controlled externally
        self.executor: Optional[concurrent.futures.ThreadPoolExecutor] = None

    def _get(self, path: str):
        key = ("GET", path)
        with self._lock:
            if key in self._cache:
                print(f"[api] cache hit: {path}")
                return self._cache[key]
        url = f"{self.base_url}{path}"
        print(f"[api] fetching: {url}")
        try:
            resp = self.session.get(url, timeout=10)
            if resp.status_code == 404:
                with self._lock:
                    self._cache[key] = None
                return None
            resp.raise_for_status()
            data = resp.json()
        except Exception:
            print(f"[api] error fetching {url}")
            data = None
        # small pause to be gentle, but don't block threads too long
        if self.pause:
            time.sleep(self.pause)
        with self._lock:
            self._cache[key] = data
        return data

    def get_citizen(self, handle: str):
        return self._get(f"/api/v1/citizens/{quote(handle, safe='')}")

    def get_org(self, org_id: str, page: int = 1, per_page: int = 100):
        return self._get(f"/api/v1/orgs/{quote(org_id, safe='')}?page={page}&per_page={per_page}")

    def ensure_executor(self, max_workers: int = 8):
        if self.executor is None:
            self.executor = concurrent.futures.ThreadPoolExecutor(max_workers=max_workers)
        return self.executor

    def get_citizen_async(self, handle: str):
        ex = self.ensure_executor()
        return ex.submit(self.get_citizen, handle)

    def get_org_async(self, org_id: str, page: int = 1, per_page: int = 100):
        ex = self.ensure_executor()
        return ex.submit(self.get_org, org_id, page, per_page)


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


def extract_orgs_from_citizen(j: dict) -> Tuple[List[str], str]:
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


def extract_members_from_org(j: dict) -> Tuple[int, List[str]]:
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


def build_graph(watchlist: List[Dict[str, str]], api: APIClient, max_depth: int = 4, private_org: str = "RUINOUS", initial_graph: Optional[Dict[str, Any]] = None, concurrency: int = 8, settings: Optional[Dict[str, Any]] = None):
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
    org_members_cache: Dict[str, Any] = {}

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

    # settings defaults
    if settings is None:
        settings = {"blacklist_org": [], "deep_scan_threshold": 2000}
    else:
        if "blacklist_org" not in settings:
            settings["blacklist_org"] = []
        if "deep_scan_threshold" not in settings:
            settings["deep_scan_threshold"] = 2000

    print(f"[graph] starting BFS with {len(nodes)} seed nodes")
    seen_count = 0

    # prepare executor for concurrent requests
    api.ensure_executor(concurrency)
    executor = api.executor
    pending: Dict[concurrent.futures.Future, tuple] = {}
    orgs_submitted: Set[str] = set()

    stopped = False
    print("[graph] press 's' then Enter to stop and save progress")

    # initial loop will submit up to `concurrency` citizen fetches
    while q or pending:
        # submit citizen fetches until we reach concurrency
        while q and len(pending) < concurrency:
            cur, dist, origin = q.popleft()
            if dist >= max_depth:
                continue
            print(f"[graph] queue submit: {cur} (dist={dist})")
            fut = api.get_citizen_async(cur)
            pending[fut] = ("citizen", cur, dist, origin)

        if not pending:
            break

        # wait for at least one future to complete so we can process it
        done, _ = concurrent.futures.wait(list(pending.keys()), return_when=concurrent.futures.FIRST_COMPLETED, timeout=1)

        # allow user to stop
        if msvcrt and msvcrt.kbhit():
            try:
                ch = msvcrt.getwch()
            except Exception:
                ch = None
            if ch and ch.lower() == "s":
                print("[graph] stop requested by user")
                stopped = True
                break

        for fut in list(done):
            info = pending.pop(fut)
            typ = info[0]
            try:
                res = fut.result()
            except Exception:
                res = None

            if typ == "citizen":
                _, cur, dist, origin = info
                seen_count += 1
                if seen_count % 20 == 0:
                    print(f"[graph] processed {seen_count} items; pending {len(pending)}; queue {len(q)}")
                print(f"[graph] expanding: {cur} (dist={dist})")

                cdata = res
                if cdata is None:
                    print(f"[graph] no data for citizen {cur}")
                    continue

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

                # direct connections
                conns = extract_connections_from_citizen(cdata)
                for other in conns:
                    edges.append({"source": cur, "target": other, "type": "connection"})
                    
                    new_dist = dist + 1
                    prev = visited_distance.get(other)
                    if prev is None or new_dist < prev:
                        visited_distance[other] = new_dist
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
                            q.append((other, new_dist, origin))
                        elif new_dist < prev:
                            print(f"[graph] updating node: {other} (dist={new_dist})")
                            nodes[other]["distance"] = min(nodes[other].get("distance", new_dist), new_dist)
                            nodes[other]["risk_level"] = assign_risk(nodes[other]["distance"])
                            q.append((other, new_dist, origin))
                        

                # expand via orgs (submit org fetches if not cached)
                for org in orgs:
                    if org == private_org and nodes[cur]["distance"] < 2:
                        nodes[cur]["alert"] = True
                    print(f"[graph] org check: {org} for {cur}")
                    if org in org_members_cache:
                        print(f"[graph] org cache hit: {org}")
                        members = org_members_cache[org]["members"]
                        total = org_members_cache[org].get("total", len(members))
                        print(f"[graph] org {org} has {total} members, total pages: {(total // 100) + 1}")
                        if total > len(members):
                            print(f"[graph] org {org} has more members ({total}) than cached ({len(members)}), total pages: {(total // 100) + 1}")
                            # pagination not implemented: skipping extra pages
                            print(f"[graph] skipping pagination for org {org}")
                        # process members from cache
                        for m in members:
                            edges.append({"source": cur, "target": m, "type": "org_member", "org": org})
                            # print(f"[graph] org member (cache): {cur} -> {m} via {org}")
                            new_dist = dist + 1
                            prev = visited_distance.get(m)
                            if prev is None or new_dist < prev:
                                print (f"[graph] org member (cache): {cur} -> {m} via {org} (dist={new_dist})")
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
                    else:
                        # if org is blacklisted in settings, skip deep scan
                        if org in settings.get("blacklist_org", []):
                            print(f"[graph] org {org} is blacklisted by settings; skipping member expansion")
                            org_members_cache[org] = {"total": 0, "members": [], "excluded": True}
                            continue
                        if org not in orgs_submitted:
                            ofut = api.get_org_async(org)
                            pending[ofut] = ("org", org, cur, dist, origin, main_org)
                            orgs_submitted.add(org)

            elif typ == "org":
                # pending org response
                _, org, parent_cur, parent_dist, origin, parent_main_org = info
                orgdata = res
                if orgdata is None:
                    print(f"[graph] no data for org {org}")
                    org_members_cache[org] = {"total": 0, "members": []}
                    continue
                total_members, members = extract_members_from_org(orgdata)
                # if the org is too large, mark as excluded and do not expand members
                if total_members >= int(settings.get("deep_scan_threshold", 2000)):
                    print(f"[graph] org {org} has {total_members} members >= threshold {settings.get('deep_scan_threshold')}; excluding from deep scan")
                    org_members_cache[org] = {"total": total_members, "members": [], "excluded": True}
                    # optionally add to in-memory blacklist
                    bl = settings.setdefault("blacklist_org", [])
                    if org not in bl:
                        bl.append(org)
                    continue
                if (org in org_members_cache):
                    org_entry = org_members_cache[org]
                    org_entry["members"].extend([m for m in members if m not in org_entry.get("members", [])])
                
                if (org not in org_members_cache) or (org_members_cache[org].get("total", 0) < total_members) or (len(org_members_cache[org].get("members", [])) < len(members)):
                    org_members_cache.setdefault(org, {"total": total_members, "members": []})
                    org_members_cache[org]["total"] = max(org_members_cache[org].get("total", 0), total_members)
                    # extend unique members
                    existing = set(org_members_cache[org].get("members", []))
                    new_members = [m for m in members if m not in existing]
                    org_members_cache[org]["members"].extend(new_members)
                    pages = (total_members // 100) + 1
                    for p in range(2, pages + 1):
                        if len(org_members_cache[org]["members"]) >= total_members:
                            print(f"[graph] already have all members for org {org} (total: {total_members}); skipping page {p}")
                            break
                        print(f"[graph] submitting page {p} for org {org} (total members: {total_members})")
                        ofut = api.get_org_async(org, page=p, per_page=100)
                        pending[ofut] = ("org", org, parent_cur, parent_dist, origin, parent_main_org)
                    
                print(f"[graph] org fetched: {org} (members={len(members)})")
                for m in members:
                    edges.append({"source": parent_cur, "target": m, "type": "org_member", "org": org})
                    print(f"[graph] org member: {parent_cur} -> {m} via {org}")
                    new_dist = parent_dist + 1
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
                                "main_org": parent_main_org,
                            }
                            print(f"[graph] discovered member node: {m} (dist={new_dist})")
                        else:
                            nodes[m]["orgs"] = list(set(nodes[m].get("orgs", []) + [org]))
                            nodes[m]["distance"] = min(nodes[m].get("distance", new_dist), new_dist)
                            nodes[m]["risk_level"] = assign_risk(nodes[m]["distance"])
                        q.append((m, new_dist, origin))

    # shutdown executor if we created it
    try:
        if api.executor:
            api.executor.shutdown(wait=False)
    except Exception:
        pass

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
    p.add_argument("--settings", default="watchlist.json", help="Path to settings JSON with blacklist_org and deep_scan_threshold")
    p.add_argument("--base-url", default=DEFAULT_BASE_URL)
    p.add_argument("--token", default=None)
    p.add_argument("--max-depth", type=int, default=4, help="Max BFS depth (>=3 collapses to level 4)")
    p.add_argument("--concurrency", type=int, default=8, help="Number of concurrent API requests")
    args = p.parse_args()

    wl = read_watchlist(args.watchlist)
    api = APIClient(args.base_url, args.token)
    # load settings file (blacklist_org, deep_scan_threshold)
    settings = {"blacklist_org": [], "deep_scan_threshold": 2000}
    try:
        with open(args.settings, "r", encoding="utf-8") as fh:
            cfg = json.load(fh)
            if isinstance(cfg, dict):
                settings.update(cfg)
        print(f"[main] loaded settings from {args.settings}")
    except Exception:
        print(f"[main] no settings file at {args.settings}, using defaults")
    initial_graph = None
    if args.resume:
        try:
            with open(args.resume, "r", encoding="utf-8") as fh:
                initial_graph = json.load(fh)
            print(f"[main] loaded resume graph from {args.resume}")
        except Exception as e:
            print(f"[main] failed to load resume file {args.resume}: {e}")

    graph = build_graph(wl, api, max_depth=args.max_depth, private_org=args.private_org, initial_graph=initial_graph, concurrency=args.concurrency, settings=settings)

    with open(args.output, "w", encoding="utf-8") as fh:
        json.dump(graph, fh, indent=2, ensure_ascii=False)
    print(f"Wrote graph to {args.output}")


if __name__ == "__main__":
    main()

