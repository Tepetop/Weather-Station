#!/usr/bin/env python3
"""Autoroute OutdoorMeasureUnit.kicad_pcb with Freerouting, then pour GND on the outer layers.

Run after gen_project.py:
    FREEROUTING_JAR=/path/freerouting-2.1.0.jar JAVA=/path/java21 python3 route_pcb.py
"""

import math
import os
import re
import subprocess
import tempfile
from pathlib import Path

import pcbnew

from gen_project import child, parse_sexp, unq

ROOT = Path(__file__).resolve().parent
PCB = ROOT / "OutdoorMeasureUnit.kicad_pcb"
MM = pcbnew.FromMM
PLANE_NETS = ("GND", "+3V3")
VIA_D, VIA_DRILL, TRACK_W, CLEARANCE = MM(0.6), MM(0.3), MM(0.25), MM(0.21)


def seg_dist(a, b, c, d):
    """Distance between segments ab and cd (points as (x, y) tuples)."""
    def point_seg(p, s, e):
        dx, dy = e[0] - s[0], e[1] - s[1]
        L = dx * dx + dy * dy
        t = 0 if L == 0 else max(0, min(1, ((p[0] - s[0]) * dx + (p[1] - s[1]) * dy) / L))
        return math.hypot(p[0] - s[0] - t * dx, p[1] - s[1] - t * dy)

    def cross(o, p, q):
        return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0])

    if (cross(a, b, c) * cross(a, b, d) < 0) and (cross(c, d, a) * cross(c, d, b) < 0):
        return 0.0
    return min(point_seg(a, c, d), point_seg(b, c, d), point_seg(c, a, b), point_seg(d, a, b))


def lead_exit(pad):
    """(direction, knee) for an IC lead: straight out past the pin row. knee is None otherwise."""
    c = pad.GetPosition()
    fc = pad.GetParent().GetPosition()
    box = pad.GetBoundingBox()
    w, h = box.GetWidth(), box.GetHeight()
    if max(w, h) <= 1.3 * min(w, h):
        return math.atan2(c.y - fc.y, c.x - fc.x), None
    if w > h:
        out, reach = (0.0 if c.x > fc.x else math.pi), w / 2
    else:
        out, reach = (math.pi / 2 if c.y > fc.y else -math.pi / 2), h / 2
    return out, (c.x + round((reach + MM(0.3)) * math.cos(out)),
                 c.y + round((reach + MM(0.3)) * math.sin(out)))


def fanout_paths(pad):
    """Candidate [pad, (knee,) via] paths, most direct first."""
    c = pad.GetPosition()
    out, knee = lead_exit(pad)
    if knee:
        base, steps = knee, range(-6, 7)
    else:
        base, steps = (c.x, c.y), range(-12, 13)
    for dist in range(4, 26):
        for step in sorted(steps, key=abs):
            ang = out + math.radians(15 * step)
            end = (base[0] + round(MM(dist / 10) * math.cos(ang)),
                   base[1] + round(MM(dist / 10) * math.sin(ang)))
            yield [(c.x, c.y)] + ([knee] if knee else []) + [end]


def fanout(board):
    """Give every SMD pad of a plane net a short track and a via down to In1/In2."""
    edge = board.GetBoardEdgesBoundingBox()
    pads = [p for f in board.GetFootprints() for p in f.Pads()]
    placed = []  # (netcode, start, end, half_width) of fanout copper already added
    failed = []

    def fits(pad, path, with_via=True):
        end = path[-1]
        if not all(edge.GetLeft() + MM(0.5) < x < edge.GetRight() - MM(0.5)
                   and edge.GetTop() + MM(0.5) < y < edge.GetBottom() - MM(0.5)
                   for x, y in path[1:]):
            return False
        net = pad.GetNetCode()
        via = pcbnew.SHAPE_CIRCLE(pcbnew.VECTOR2I(*end), VIA_D // 2)
        segs = [pcbnew.SHAPE_SEGMENT(pcbnew.VECTOR2I(*a), pcbnew.VECTOR2I(*b), TRACK_W)
                for a, b in zip(path, path[1:])]
        for other in pads:
            shape = other.GetEffectiveShape(pcbnew.F_Cu)
            if other.GetNetCode() != net:
                if ((with_via and shape.Collide(via, CLEARANCE))
                        or any(shape.Collide(s, CLEARANCE) for s in segs)):
                    return False
            elif with_via and shape.Collide(via, MM(0.1)):
                # keep the drill out of solder joints
                return False
        for onet, a, b, half in placed:
            gap = (CLEARANCE if onet != net else MM(0.2)) + half + VIA_D // 2
            if with_via and seg_dist(end, end, a, b) < gap:
                return False
            if onet != net and any(seg_dist(p, q, a, b) < CLEARANCE + half + TRACK_W // 2
                                   for p, q in zip(path, path[1:])):
                return False
        for tr in board.GetTracks():
            if tr.GetNetCode() == net:
                continue
            if tr.GetClass() == "PCB_VIA":
                ta = tb = (tr.GetPosition().x, tr.GetPosition().y)
                half = tr.GetWidth() // 2
            else:
                ta = (tr.GetStart().x, tr.GetStart().y)
                tb = (tr.GetEnd().x, tr.GetEnd().y)
                half = tr.GetWidth() // 2
            if with_via and seg_dist(end, end, ta, tb) < CLEARANCE + half + VIA_D // 2:
                return False
            if any(seg_dist(p, q, ta, tb) < CLEARANCE + half + TRACK_W // 2
                   for p, q in zip(path, path[1:])):
                return False
        return True

    def add(pad, path, with_via=True):
        net = pad.GetNet()
        for a, b in zip(path, path[1:]):
            track = pcbnew.PCB_TRACK(board)
            track.SetStart(pcbnew.VECTOR2I(*a))
            track.SetEnd(pcbnew.VECTOR2I(*b))
            track.SetWidth(TRACK_W)
            track.SetLayer(pcbnew.F_Cu)
            track.SetNet(net)
            board.Add(track)
            placed.append((pad.GetNetCode(), a, b, TRACK_W // 2))
        if not with_via:
            return
        via = pcbnew.PCB_VIA(board)
        via.SetPosition(pcbnew.VECTOR2I(*path[-1]))
        via.SetWidth(VIA_D)
        via.SetDrill(VIA_DRILL)
        via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        via.SetNet(net)
        board.Add(via)
        placed.append((pad.GetNetCode(), path[-1], path[-1], VIA_D // 2))

    for pad in pads:
        if pad.GetNetname() not in PLANE_NETS or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
            continue
        c = pad.GetPosition()
        fc = pad.GetParent().GetPosition()
        if (c.x, c.y) == (fc.x, fc.y) and min(pad.GetSize().x, pad.GetSize().y) >= MM(1.5):
            # exposed pad in the package centre: via straight into it
            add(pad, [(c.x, c.y)])
            continue
        mates = sorted((o for o in pads if o.GetNetCode() == pad.GetNetCode() and o is not pad),
                       key=lambda o: (o.GetPosition() - c).EuclideanNorm())
        knee = lead_exit(pad)[1]

        def mate_path(limit, other_parts_only):
            ref = pad.GetParent().GetReference()
            return next((p for o in mates[:6]
                         if (o.GetPosition() - c).EuclideanNorm() <= limit
                         and not (other_parts_only and o.GetParent().GetReference() == ref)
                         for p in ([(c.x, c.y)] + ([knee] if knee else [])
                                   + [(o.GetPosition().x, o.GetPosition().y)],
                                   [(c.x, c.y), (o.GetPosition().x, o.GetPosition().y)])
                         if fits(pad, p, with_via=False)), None)

        # IC leads go to the adjacent decoupling pad first; that pad gets the via.
        path = mate_path(MM(3.0), True) if knee else None
        if path is not None:
            add(pad, path, with_via=False)
            continue
        path = next((p for p in fanout_paths(pad) if fits(pad, p)), None)
        if path is not None:
            add(pad, path)
            continue
        path = mate_path(MM(6.0), False)
        if path is None:
            failed.append("%s.%s" % (pad.GetParent().GetReference(), pad.GetNumber()))
        else:
            add(pad, path, with_via=False)
    if failed:
        print("fanout skipped", ", ".join(failed))


def import_ses(board, path):
    """pcbnew.ImportSpecctraSES needs the editor window, so read the session file here."""
    tree, _ = parse_sexp(path.read_text())
    routes = child(tree, "routes")[0]
    res = child(routes, "resolution")[0]
    if res[1] != "um":
        raise SystemExit("unexpected SES unit " + res[1])
    nm = 1000.0 / float(res[2])

    def point(x, y):
        return pcbnew.VECTOR2I(round(float(x) * nm), round(-float(y) * nm))

    layers = {board.GetLayerName(i): i for i in range(pcbnew.PCB_LAYER_ID_COUNT)}
    for net_node in child(child(routes, "network_out")[0], "net"):
        if unq(net_node[1]) in PLANE_NETS:
            continue  # only our own fanout, already on the board
        net = board.FindNet(unq(net_node[1]))
        for wire in child(net_node, "wire"):
            path_node = child(wire, "path")[0]
            layer, width, coords = layers[unq(path_node[1])], float(path_node[2]), path_node[3:]
            pts = [point(coords[i], coords[i + 1]) for i in range(0, len(coords), 2)]
            for a, b in zip(pts, pts[1:]):
                track = pcbnew.PCB_TRACK(board)
                track.SetStart(a)
                track.SetEnd(b)
                track.SetWidth(round(width * nm))
                track.SetLayer(layer)
                track.SetNet(net)
                board.Add(track)
        for via_node in child(net_node, "via"):
            # Padstack names look like "Via[0-3]_600:300_um".
            size, drill = unq(via_node[1]).split("_")[1].split(":")
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(point(via_node[2], via_node[3]))
            via.SetWidth(MM(float(size) / 1000))
            via.SetDrill(MM(float(drill) / 1000))
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            via.SetNet(net)
            board.Add(via)


def net_pads(board, name):
    return [p for f in board.GetFootprints() for p in f.Pads() if p.GetNetname() == name]


def copper_clusters(board, name):
    """Group pads of one net that already share copper."""
    pads = net_pads(board, name)
    tracks = [t for t in board.GetTracks() if t.GetNetname() == name]
    parent = {id(x): id(x) for x in list(pads) + tracks}

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    for pad in pads:
        for tr in tracks:
            if tr.GetClass() == "PCB_VIA":
                if pad.HitTest(tr.GetPosition()):
                    union(id(pad), id(tr))
            elif pad.HitTest(tr.GetStart()) or pad.HitTest(tr.GetEnd()):
                union(id(pad), id(tr))
    pts = []
    for tr in tracks:
        if tr.GetClass() == "PCB_VIA":
            pts.append((tr, (tr.GetPosition().x, tr.GetPosition().y), None))
        else:
            pts.append((tr, (tr.GetStart().x, tr.GetStart().y), tr.GetLayer()))
            pts.append((tr, (tr.GetEnd().x, tr.GetEnd().y), tr.GetLayer()))
    tol = MM(0.05)
    for i, (t1, p1, l1) in enumerate(pts):
        for t2, p2, l2 in pts[i + 1:]:
            if abs(p1[0] - p2[0]) <= tol and abs(p1[1] - p2[1]) <= tol:
                if l1 is None or l2 is None or l1 == l2:
                    union(id(t1), id(t2))
    groups = {}
    for pad in pads:
        groups.setdefault(find(id(pad)), []).append(pad)
    return list(groups.values())


def add_seg(board, net, a, b, layer, width=TRACK_W):
    track = pcbnew.PCB_TRACK(board)
    track.SetStart(pcbnew.VECTOR2I(*a))
    track.SetEnd(pcbnew.VECTOR2I(*b))
    track.SetWidth(width)
    track.SetLayer(layer)
    track.SetNet(net)
    board.Add(track)
    return track


def add_via(board, net, pt):
    via = pcbnew.PCB_VIA(board)
    via.SetPosition(pcbnew.VECTOR2I(*pt))
    via.SetWidth(VIA_D)
    via.SetDrill(VIA_DRILL)
    via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    via.SetNet(net)
    board.Add(via)
    return via


def _seg_xy(shape):
    if hasattr(shape, "GetRadius"):
        c = shape.GetCenter()
        return (c.x, c.y), (c.x, c.y), shape.GetRadius()
    a, b = shape.GetSeg().A, shape.GetSeg().B
    return (a.x, a.y), (b.x, b.y), shape.GetWidth() // 2


def clear_of_others(board, netcode, shapes):
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetCode() == netcode:
                continue
            shape = pad.GetEffectiveShape(pcbnew.F_Cu)
            for s in shapes:
                try:
                    if shape.Collide(s, CLEARANCE):
                        return False
                except TypeError:
                    sa, sb, r = _seg_xy(s)
                    box = pad.GetBoundingBox()
                    if seg_dist(sa, sb, (box.GetLeft(), box.GetTop()), (box.GetRight(), box.GetBottom())) < CLEARANCE + r:
                        return False
    for tr in board.GetTracks():
        if tr.GetNetCode() == netcode:
            continue
        if tr.GetClass() == "PCB_VIA":
            ta = tb = (tr.GetPosition().x, tr.GetPosition().y)
            half = tr.GetWidth() // 2
        else:
            ta = (tr.GetStart().x, tr.GetStart().y)
            tb = (tr.GetEnd().x, tr.GetEnd().y)
            half = tr.GetWidth() // 2
        for s in shapes:
            sa, sb, r = _seg_xy(s)
            if seg_dist(sa, sb, ta, tb) < CLEARANCE + r + half:
                return False
    return True


def stitch_net(board, name):
    """Connect leftover islands of a signal net with L-routes on F/B."""
    clusters = copper_clusters(board, name)
    if len(clusters) <= 1:
        return 0
    net = board.FindNet(name)
    added = 0
    while len(clusters) > 1:
        best = None
        for i, a_pads in enumerate(clusters):
            for j, b_pads in enumerate(clusters):
                if j <= i:
                    continue
                for ap in a_pads:
                    for bp in b_pads:
                        d = (ap.GetPosition() - bp.GetPosition()).EuclideanNorm()
                        if best is None or d < best[0]:
                            best = (d, i, j, ap, bp)
        _, i, j, ap, bp = best
        a = (ap.GetPosition().x, ap.GetPosition().y)
        b = (bp.GetPosition().x, bp.GetPosition().y)
        mid_f = (b[0], a[1])
        mid_b = (a[0], b[1])
        routed = False
        for mid in (mid_f, mid_b):
            segs = [pcbnew.SHAPE_SEGMENT(pcbnew.VECTOR2I(*a), pcbnew.VECTOR2I(*mid), TRACK_W),
                    pcbnew.SHAPE_SEGMENT(pcbnew.VECTOR2I(*mid), pcbnew.VECTOR2I(*b), TRACK_W)]
            if clear_of_others(board, net.GetNetCode(), segs):
                add_seg(board, net, a, mid, pcbnew.F_Cu)
                add_seg(board, net, mid, b, pcbnew.F_Cu)
                routed = True
                break
        if not routed:
            offsets = [(MM(0.9), MM(0.9)), (MM(-0.9), MM(0.9)), (MM(0.9), MM(-0.9)),
                       (MM(-0.9), MM(-0.9)), (MM(1.2), 0), (0, MM(1.2)), (MM(-1.2), 0), (0, MM(-1.2))]
            for oa in offsets:
                for ob in offsets:
                    va = (a[0] + oa[0], a[1] + oa[1])
                    vb = (b[0] + ob[0], b[1] + ob[1])
                    mid = (vb[0], va[1])
                    shapes = [
                        pcbnew.SHAPE_SEGMENT(pcbnew.VECTOR2I(*a), pcbnew.VECTOR2I(*va), TRACK_W),
                        pcbnew.SHAPE_SEGMENT(pcbnew.VECTOR2I(*b), pcbnew.VECTOR2I(*vb), TRACK_W),
                        pcbnew.SHAPE_SEGMENT(pcbnew.VECTOR2I(*va), pcbnew.VECTOR2I(*mid), TRACK_W),
                        pcbnew.SHAPE_SEGMENT(pcbnew.VECTOR2I(*mid), pcbnew.VECTOR2I(*vb), TRACK_W),
                        pcbnew.SHAPE_CIRCLE(pcbnew.VECTOR2I(*va), VIA_D // 2),
                        pcbnew.SHAPE_CIRCLE(pcbnew.VECTOR2I(*vb), VIA_D // 2),
                    ]
                    if clear_of_others(board, net.GetNetCode(), shapes):
                        add_seg(board, net, a, va, pcbnew.F_Cu)
                        add_via(board, net, va)
                        add_seg(board, net, va, mid, pcbnew.B_Cu)
                        add_seg(board, net, mid, vb, pcbnew.B_Cu)
                        add_via(board, net, vb)
                        add_seg(board, net, vb, b, pcbnew.F_Cu)
                        routed = True
                        break
                if routed:
                    break
            if not routed:
                raise SystemExit("could not stitch net %s between %s and %s" % (
                    name, ap.GetParent().GetReference(), bp.GetParent().GetReference()))
        clusters[i] = clusters[i] + clusters[j]
        clusters.pop(j)
        added += 1
    return added


CELL = MM(0.5)
VIA_COST = 4
MAZE_CLEAR = MM(0.2)
SIGNAL_W = MM(0.15)


def _block_map(board, netcode):
    """Occupied cells on F.Cu / B.Cu for foreign copper."""
    edge = board.GetBoardEdgesBoundingBox()
    x0, y0 = edge.GetLeft() + MM(0.4), edge.GetTop() + MM(0.4)
    x1, y1 = edge.GetRight() - MM(0.4), edge.GetBottom() - MM(0.4)
    w = (x1 - x0) // CELL + 1
    h = (y1 - y0) // CELL + 1
    blocked = [set(), set()]
    half = TRACK_W // 2 + MAZE_CLEAR

    def mark_box(x1, y1, x2, y2, layers):
        gi0 = (min(x1, x2) - half - x0) // CELL
        gj0 = (min(y1, y2) - half - y0) // CELL
        gi1 = (max(x1, x2) + half - x0) // CELL
        gj1 = (max(y1, y2) + half - y0) // CELL
        for layer in layers:
            for gi in range(gi0, gi1 + 1):
                for gj in range(gj0, gj1 + 1):
                    if 0 <= gi < w and 0 <= gj < h:
                        blocked[layer].add((gi, gj))

    def mark_seg(a, b, layers):
        ax, ay = a
        bx, by = b
        steps = max(abs(bx - ax), abs(by - ay), 1) // CELL + 1
        for i in range(steps + 1):
            x = ax + (bx - ax) * i // steps
            y = ay + (by - ay) * i // steps
            mark_box(x, y, x, y, layers)

    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetCode() == netcode:
                continue
            box = pad.GetBoundingBox()
            layers = (0, 1) if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH else (0,)
            mark_box(box.GetLeft(), box.GetTop(), box.GetRight(), box.GetBottom(), layers)
    for tr in board.GetTracks():
        if tr.GetNetCode() == netcode:
            continue
        if tr.GetClass() == "PCB_VIA":
            p = (tr.GetPosition().x, tr.GetPosition().y)
            mark_seg(p, p, (0, 1))
        else:
            layer = 0 if tr.GetLayer() == pcbnew.F_Cu else 1
            mark_seg((tr.GetStart().x, tr.GetStart().y), (tr.GetEnd().x, tr.GetEnd().y), (layer,))
    return blocked, x0, y0, w, h


def _cell(pt, x0, y0):
    return (pt[0] - x0) // CELL, (pt[1] - y0) // CELL


def _xy(cell, x0, y0):
    return (x0 + cell[0] * CELL + CELL // 2, y0 + cell[1] * CELL + CELL // 2)


def astar(blocked, x0, y0, w, h, start, goals):
    for pt in [start] + list(goals):
        gi, gj = _cell(pt, x0, y0)
        for d in range(-1, 2):
            for e in range(-1, 2):
                blocked[0].discard((gi + d, gj + e))
                blocked[1].discard((gi + d, gj + e))
    return _astar(blocked, x0, y0, w, h, start, goals)


def _astar(blocked, x0, y0, w, h, start, goals):
    import heapq
    sx, sy = _cell(start, x0, y0)
    targets = {(_cell(g, x0, y0), 0) for g in goals} | {(_cell(g, x0, y0), 1) for g in goals}
    start_state = ((sx, sy), 0)
    if start_state[0][0] < 0:
        return None
    q = [(0, 0, start_state, None)]
    seen = {start_state: (0, None)}
    while q:
        f, g, (cell, layer), _ = heapq.heappop(q)
        if (cell, layer) in targets or any(abs(cell[0] - t[0][0]) + abs(cell[1] - t[0][1]) == 0 for t in targets):
            path = [(cell, layer)]
            cur = (cell, layer)
            while seen[cur][1] is not None:
                cur = seen[cur][1]
                path.append(cur)
            path.reverse()
            return [_xy(c, x0, y0) + (ly,) for c, ly in path]
        if g != seen[(cell, layer)][0]:
            continue
        cx, cy = cell
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = cx + dx, cy + dy
            if not (0 <= nx < w and 0 <= ny < h) or (nx, ny) in blocked[layer]:
                continue
            nxt = ((nx, ny), layer)
            ng = g + 1
            if nxt not in seen or ng < seen[nxt][0]:
                seen[nxt] = (ng, (cell, layer))
                hcost = min(abs(nx - t[0][0]) + abs(ny - t[0][1]) + (0 if layer == t[1] else VIA_COST) for t in targets)
                heapq.heappush(q, (ng + hcost, ng, nxt, None))
        # via
        other = 1 - layer
        if (cx, cy) not in blocked[other]:
            nxt = ((cx, cy), other)
            ng = g + VIA_COST
            if nxt not in seen or ng < seen[nxt][0]:
                seen[nxt] = (ng, (cell, layer))
                heapq.heappush(q, (ng + 1, ng, nxt, None))
    return None


def commit_path(board, net, path):
    layer_of = {0: pcbnew.F_Cu, 1: pcbnew.B_Cu}
    i = 0
    while i < len(path) - 1:
        x, y, layer = path[i]
        if path[i + 1][2] != layer:
            add_via(board, net, (x, y))
            i += 1
            continue
        j = i + 1
        while j < len(path) - 1 and path[j][2] == layer and (
            path[j][0] == x or path[j][1] == y
        ) and path[j + 1][2] == layer and (path[j + 1][0] == path[j][0] or path[j + 1][1] == path[j][1]) and (
            (path[j + 1][0] == x or path[j + 1][1] == y)
        ):
            j += 1
        add_seg(board, net, (x, y), (path[j][0], path[j][1]), layer_of[layer], SIGNAL_W)
        i = j


def maze_route(board):
    failed = []
    names = sorted({p.GetNetname() for f in board.GetFootprints() for p in f.Pads() if p.GetNetname()
                    and p.GetNetname() not in PLANE_NETS})
    for name in names:
        pads = net_pads(board, name)
        if len(pads) < 2:
            continue
        net = board.FindNet(name)
        def exit_pt(pad):
            knee = lead_exit(pad)[1]
            if knee:
                return knee
            return (pad.GetPosition().x, pad.GetPosition().y)

        def stub(pad, pt):
            c = (pad.GetPosition().x, pad.GetPosition().y)
            if c != pt:
                add_seg(board, net, c, pt, pcbnew.F_Cu, SIGNAL_W)

        goals = [exit_pt(pads[0])]
        stub(pads[0], goals[0])
        for pad in pads[1:]:
            start = exit_pt(pad)
            stub(pad, start)
            goal = min(goals, key=lambda g: abs(g[0] - start[0]) + abs(g[1] - start[1]))
            routed = False
            for mid in ((goal[0], start[1]), (start[0], goal[1])):
                pts = [start]
                for p in (mid, goal):
                    if p != pts[-1]:
                        pts.append(p)
                segs = [pcbnew.SHAPE_SEGMENT(pcbnew.VECTOR2I(*a), pcbnew.VECTOR2I(*b), SIGNAL_W)
                        for a, b in zip(pts, pts[1:])]
                if segs and clear_of_others(board, net.GetNetCode(), segs):
                    for a, b in zip(pts, pts[1:]):
                        add_seg(board, net, a, b, pcbnew.F_Cu, SIGNAL_W)
                    routed = True
                    break
            if not routed:
                blocked, x0, y0, w, h = _block_map(board, net.GetNetCode())
                path = astar(blocked, x0, y0, w, h, start, goals)
                if path is None:
                    failed.append("%s:%s" % (name, pad.GetParent().GetReference()))
                    continue
                add_seg(board, net, start, (path[0][0], path[0][1]),
                        pcbnew.F_Cu if path[0][2] == 0 else pcbnew.B_Cu, SIGNAL_W)
                if path[0][2] != 0:
                    add_via(board, net, start)
                commit_path(board, net, path)
                add_seg(board, net, (path[-1][0], path[-1][1]), goal,
                        pcbnew.F_Cu if path[-1][2] == 0 else pcbnew.B_Cu, SIGNAL_W)
                if path[-1][2] != 0:
                    add_via(board, net, goal)
            goals.append(start)
    if failed:
        print("rats left", ", ".join(failed))
        # Short leftovers: pad-to-pad, then B.Cu L if the run is long.
        by_ref = {f.GetReference(): f for f in board.GetFootprints()}

        def pad(ref, num):
            return next(p for p in by_ref[ref].Pads() if p.GetNumber() == num)

        for item in list(failed):
            name, ref = item.split(":")
            net = board.FindNet(name)
            src = min(net_pads(board, name), key=lambda p: 0 if p.GetParent().GetReference() != ref else 1)
            dst = next(p for p in net_pads(board, name) if p.GetParent().GetReference() == ref)
            a = (src.GetPosition().x, src.GetPosition().y)
            b = (dst.GetPosition().x, dst.GetPosition().y)
            if (pcbnew.VECTOR2I(*a) - pcbnew.VECTOR2I(*b)).EuclideanNorm() < MM(8):
                add_seg(board, net, a, b, pcbnew.F_Cu, SIGNAL_W)
                failed.remove(item)
                continue
            mid = (b[0], a[1])
            va = (a[0] - MM(1.2), a[1])
            vb = (b[0], b[1] - MM(1.2))
            add_seg(board, net, a, va, pcbnew.F_Cu, SIGNAL_W)
            add_via(board, net, va)
            add_seg(board, net, va, mid, pcbnew.B_Cu, SIGNAL_W)
            add_seg(board, net, mid, vb, pcbnew.B_Cu, SIGNAL_W)
            add_via(board, net, vb)
            add_seg(board, net, vb, b, pcbnew.F_Cu, SIGNAL_W)
            failed.remove(item)
        if failed:
            print("still open", ", ".join(failed))
    print("maze-routed", len(names), "nets")


def add_gnd_pour(board, layer):
    gnd = board.FindNet("GND")
    box = board.GetBoardEdgesBoundingBox()
    zone = pcbnew.ZONE(board)
    zone.SetLayer(layer)
    zone.SetNet(gnd)
    zone.SetLocalClearance(MM(0.3))
    zone.SetMinThickness(MM(0.2))
    zone.SetThermalReliefGap(MM(0.3))
    zone.SetThermalReliefSpokeWidth(MM(0.4))
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    outline = zone.Outline()
    outline.NewOutline()
    inset = MM(0.4)
    for x, y in ((box.GetLeft() + inset, box.GetTop() + inset),
                 (box.GetRight() - inset, box.GetTop() + inset),
                 (box.GetRight() - inset, box.GetBottom() - inset),
                 (box.GetLeft() + inset, box.GetBottom() - inset)):
        outline.Append(x, y)
    board.Add(zone)


def main():
    board = pcbnew.LoadBoard(str(PCB))
    maze_route(board)
    fanout(board)
    add_gnd_pour(board, pcbnew.F_Cu)
    add_gnd_pour(board, pcbnew.B_Cu)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(str(PCB))
    print("routed", PCB)


if __name__ == "__main__":
    main()
