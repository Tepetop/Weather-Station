#!/usr/bin/env python3
"""Generate the OutdoorMeasureUnit KiCad 7 schematic and PCB."""

import json
import math
import shutil
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SYM = Path("/usr/share/kicad/symbols")
FP = Path("/usr/share/kicad/footprints")
PROJECT = "OutdoorMeasureUnit"
SHEET = "7c1a9e2b-4d55-4a1e-9c30-6b8f0a11d4e2"

R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
L2016 = "Inductor_SMD:L_Cenker_CKCS201610"
LED0402 = "LED_SMD:LED_0402_1005Metric"
XTAL = "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm"


def uid():
    return str(uuid.uuid4())


def q(text):
    return '"' + str(text).replace("\\", "\\\\").replace('"', '\\"') + '"'


def unq(token):
    if isinstance(token, str) and len(token) >= 2 and token[0] == '"':
        return token[1:-1].replace('\\"', '"').replace("\\\\", "\\")
    return token


def parse_sexp(text, start=0):
    n = len(text)

    def skip(i):
        while i < n and text[i] in " \t\r\n":
            i += 1
        return i

    def value(i):
        i = skip(i)
        if text[i] == "(":
            items = []
            i += 1
            while True:
                i = skip(i)
                if text[i] == ")":
                    return items, i + 1
                item, i = value(i)
                items.append(item)
        if text[i] == '"':
            i += 1
            buf = []
            while True:
                c = text[i]
                if c == "\\":
                    buf.append(text[i : i + 2])
                    i += 2
                    continue
                if c == '"':
                    return '"' + "".join(buf) + '"', i + 1
                buf.append(c)
                i += 1
        j = i
        while j < n and text[j] not in " \t\r\n()":
            j += 1
        return text[i:j], j

    return value(skip(start))


def dump(node, indent=0):
    if not isinstance(node, list):
        return node
    pad = "  " * indent
    if not node:
        return "()"
    head = node[0] if not isinstance(node[0], list) else None
    short = head in {
        "at", "xy", "pts", "size", "font", "justify", "stroke", "fill",
        "effects", "uuid", "paper", "unit", "in_bom", "on_board", "dnp",
        "fields_autoplaced", "length", "name", "number", "hide", "pin",
    }
    parts = []
    for item in node:
        parts.append(dump(item, indent + 1) if isinstance(item, list) else item)
    if short or sum(len(p) for p in parts) < 90:
        return "(" + " ".join(parts) + ")"
    inner = "\n".join(pad + "  " + p for p in parts)
    return "(\n" + inner + "\n" + pad + ")"


_FILE_CACHE = {}


def extract_symbol(filename, name):
    path = SYM / filename
    text = _FILE_CACHE.get(path)
    if text is None:
        text = path.read_text(encoding="utf-8", errors="replace")
        _FILE_CACHE[path] = text
    needle = '\n  (symbol "%s"' % name
    idx = text.find(needle)
    if idx < 0 and text.startswith('(symbol "%s"' % name):
        idx = -1
    if idx < 0:
        raise KeyError(name)
    tree, _ = parse_sexp(text, idx + 1)
    return tree


def child(node, key):
    found = []
    for item in node:
        if isinstance(item, list) and item and item[0] == key:
            found.append(item)
    return found


def flatten(filename, name, seen=None):
    seen = seen or set()
    if name in seen:
        raise RuntimeError("extends loop " + name)
    seen.add(name)
    sym = extract_symbol(filename, name)
    ext = child(sym, "extends")
    if not ext:
        return sym
    parent_name = unq(ext[0][1])
    parent = flatten(filename, parent_name, seen)
    props = {}
    for prop in child(parent, "property"):
        props[unq(prop[1])] = prop
    for prop in child(sym, "property"):
        props[unq(prop[1])] = prop
    out = ["symbol", q(name)]
    for item in sym[2:]:
        if isinstance(item, list) and item and item[0] in ("property", "extends", "symbol"):
            continue
        out.append(item)
    # parent flags such as pin_names if the child omitted them
    for item in parent[2:]:
        if isinstance(item, list) and item and item[0] in (
            "pin_numbers", "pin_names", "in_bom", "on_board", "power",
        ):
            if not child(out, item[0]):
                out.append(item)
    for prop in props.values():
        out.append(prop)
    for item in parent:
        if isinstance(item, list) and item and item[0] == "symbol":
            unit = item[:]
            old = unq(unit[1])
            prefix = parent_name + "_"
            if old.startswith(prefix):
                unit[1] = q(name + old[len(prefix) - 1 :])
            out.append(unit)
    return out


def lib_symbol(filename, name, lib):
    sym = flatten(filename, name)
    sym[1] = q("%s:%s" % (lib, name))
    return sym


def custom_symbol(name, body_w, pins, ref_prefix="U"):
    """pins: list of (number, name, x, y, angle, etype)."""
    sym = [
        "symbol", q("OutdoorMeasureUnit:" + name),
        ["pin_names", ["offset", "1.016"]],
        ["in_bom", "yes"], ["on_board", "yes"],
        ["property", q("Reference"), q(ref_prefix), ["at", "0", "10.16", "0"],
         ["effects", ["font", ["size", "1.27", "1.27"]], ["justify", "left"]]],
        ["property", q("Value"), q(name), ["at", "0", "7.62", "0"],
         ["effects", ["font", ["size", "1.27", "1.27"]], ["justify", "left"]]],
        ["property", q("Footprint"), q(""), ["at", "0", "0", "0"],
         ["effects", ["font", ["size", "1.27", "1.27"]], "hide"]],
        ["property", q("Datasheet"), q(""), ["at", "0", "0", "0"],
         ["effects", ["font", ["size", "1.27", "1.27"]], "hide"]],
    ]
    ys = [p[3] for p in pins]
    xs = [p[2] for p in pins]
    graphics = ["symbol", q(name + "_0_1"),
                ["rectangle",
                 ["start", str(min(xs) + 2.54), str(max(ys) + 1.27)],
                 ["end", str(max(xs) - 2.54), str(min(ys) - 1.27)],
                 ["stroke", ["width", "0.254"], ["type", "default"]],
                 ["fill", ["type", "background"]]]]
    pin_unit = ["symbol", q(name + "_1_1")]
    for number, pname, x, y, angle, etype in pins:
        pin_unit.append([
            "pin", etype, "line",
            ["at", str(x), str(y), str(angle)],
            ["length", "2.54"],
            ["name", q(pname), ["effects", ["font", ["size", "1.27", "1.27"]]]],
            ["number", q(str(number)), ["effects", ["font", ["size", "1.27", "1.27"]]]],
        ])
    sym.append(graphics)
    sym.append(pin_unit)
    return sym


def pins_of(sym):
    pins = []
    for unit in child(sym, "symbol"):
        for pin in child(unit, "pin"):
            at = child(pin, "at")[0]
            number = unq(child(pin, "number")[0][1])
            name = unq(child(pin, "name")[0][1])
            pins.append({
                "number": number,
                "name": name,
                "x": float(at[1]),
                "y": float(at[2]),
                "angle": float(at[3]),
                "hide": "hide" in pin,
            })
    return pins


def rot(x, y, deg):
    """Rotate in the symbol's Y-up coordinates."""
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return x * c - y * s, x * s + y * c


def sheet_xy(px, py, sx, sy, srot):
    """Symbol libraries are Y-up. The schematic sheet is Y-down."""
    rx, ry = rot(px, py, srot)
    return sx + rx, sy - ry


def sheet_delta(dx, dy):
    return dx, -dy


def r2(value):
    return str(round(value, 2)).rstrip("0").rstrip(".") if "." in str(round(value, 2)) else str(round(value, 2))


def fmt(value):
    rounded = round(value, 2)
    text = f"{rounded:.2f}".rstrip("0").rstrip(".")
    if text == "-0":
        return "0"
    return text


class Schematic:
    def __init__(self):
        self.libs = []
        self.lib_ids = set()
        self.parts = []
        self.draw = []
        self.pwr_n = 0
        self.flg_n = 0

    def add_lib(self, sym):
        lib_id = unq(sym[1])
        if lib_id not in self.lib_ids:
            self.lib_ids.add(lib_id)
            self.libs.append(sym)
        return lib_id

    def add_part(self, lib_id, ref, value, footprint, at, rot_deg, pin_nets, sym):
        self.parts.append({
            "lib_id": lib_id,
            "ref": ref,
            "value": value,
            "footprint": footprint,
            "at": at,
            "rot": rot_deg,
            "pin_nets": {str(k): v for k, v in pin_nets.items()},
            "sym": sym,
            "uuid": uid(),
        })

    def place_power(self, kind, at, rot_deg=0):
        if kind == "PWR_FLAG":
            self.flg_n += 1
            ref = "#FLG%02d" % self.flg_n
            lib_id = "power:PWR_FLAG"
        else:
            self.pwr_n += 1
            ref = "#PWR%02d" % self.pwr_n
            lib_id = "power:" + kind
        sym = self.cached(lib_id)
        self.add_part(lib_id, ref, kind if kind != "PWR_FLAG" else "PWR_FLAG",
                      "", at, rot_deg, {"1": None}, sym)
        return ref

    def cached(self, lib_id):
        for sym in self.libs:
            if unq(sym[1]) == lib_id:
                return sym
        raise KeyError(lib_id)

    def emit_connections(self):
        occupied = {}
        for part in self.parts:
            if part["ref"].startswith("#"):
                continue
            pins = pins_of(part["sym"])
            known = {p["number"] for p in pins}
            missing = set(part["pin_nets"]) - known
            extra = known - set(part["pin_nets"])
            if missing or extra:
                raise SystemExit("%s pin map mismatch missing=%s extra=%s" % (
                    part["ref"], sorted(missing), sorted(extra)))
            for pin in pins:
                net = part["pin_nets"][pin["number"]]
                wx, wy = sheet_xy(pin["x"], pin["y"], part["at"][0], part["at"][1], part["rot"])
                key = (fmt(wx), fmt(wy))
                occupied.setdefault(key, []).append((part["ref"], pin["number"], net))
                if net is None:
                    continue
                ang = (pin["angle"] + 180 + part["rot"]) % 360
                dx, dy = sheet_delta(*rot(2.54, 0, ang))
                ox, oy = wx + dx, wy + dy
                self.draw.append(
                    "(wire (pts (xy %s %s) (xy %s %s))\n"
                    "  (stroke (width 0) (type solid))\n  (uuid %s))" % (
                        fmt(wx), fmt(wy), fmt(ox), fmt(oy), uid()))
                if net in ("GND", "+3V3"):
                    spin = 0 if ang in (0, 180) else 0
                    # GND symbol hangs in -Y from its pin. Point that away from the IC.
                    prot = (ang + 270) % 360
                    if net == "+3V3":
                        prot = (ang + 90) % 360
                    self.place_power(net, (round(ox, 2), round(oy, 2)), prot)
                elif net == "PWR":
                    self.place_power("PWR_FLAG", (round(ox, 2), round(oy, 2)), 0)
                else:
                    just = "left" if dx >= 0 else "right"
                    self.draw.append(
                        '(label %s (at %s %s 0)\n'
                        '  (effects (font (size 1.27 1.27)) (justify %s bottom))\n'
                        '  (uuid %s))' % (q(net), fmt(ox), fmt(oy), just, uid()))
        for key, items in occupied.items():
            nets = {net for _, _, net in items}
            if len(nets) > 1:
                raise SystemExit("stacked pins disagree at %s: %s" % (key, items))
            if nets == {None}:
                self.draw.append("(no_connect (at %s %s) (uuid %s))" % (key[0], key[1], uid()))

    def write(self, path):
        self.emit_connections()
        chunks = [
            "(kicad_sch (version 20230121) (generator eeschema)",
            "  (uuid %s)" % SHEET,
            '  (paper "A2")',
            "  (title_block",
            '    (title "OutdoorMeasureUnit")',
            '    (date "2026-09-27")',
            '    (rev "1.2")',
            "    (comment 1 \"STM32F103 weather station. Charge stays at 4.20 V (MCP73871-2CC).\")",
            "    (comment 2 \"3.3 V from TPS62840 buck, input is charger OUT. R15 267k sets 3.3 V.\")",
            "    (comment 3 \"Crystal load caps are 18 pF; change them to match the crystal CL.\")",
            "    (comment 4 \"LowBAT is PA0 (WKUP), matching the EasyEDA schematic. FHDW01A lock is 4.28 V.\")",
            "  )",
            "  (lib_symbols",
        ]
        for sym in self.libs:
            chunks.append(dump(sym, 2))
        chunks.append("  )")
        for part in self.parts:
            x, y = part["at"]
            pins = pins_of(part["sym"])
            if pins and not part["ref"].startswith("#"):
                worlds = []
                for pin in pins:
                    wx, wy = sheet_xy(pin["x"], pin["y"], x, y, part["rot"])
                    worlds.append((wx, wy))
                top = max(p[1] for p in worlds)
                ref_at = (x, top + 2.54)
                val_at = (x, min(p[1] for p in worlds) - 2.54)
            else:
                ref_at = (x, y - 3.81)
                val_at = (x, y + 3.81 if part["value"] == "+3V3" else y - 3.81)
            hide_ref = " hide" if part["ref"].startswith("#") else ""
            pin_lines = "\n".join(
                '    (pin %s (uuid %s))' % (q(p["number"]), uid()) for p in pins)
            chunks.append(
                "  (symbol (lib_id %s) (at %s %s %s) (unit 1)\n"
                "    (in_bom %s) (on_board %s) (dnp no)\n"
                "    (uuid %s)\n"
                "    (property \"Reference\" %s (at %s %s 0)\n"
                "      (effects (font (size 1.27 1.27))%s))\n"
                "    (property \"Value\" %s (at %s %s 0)\n"
                "      (effects (font (size 1.27 1.27))%s))\n"
                "    (property \"Footprint\" %s (at %s %s 0)\n"
                "      (effects (font (size 1.27 1.27)) hide))\n"
                "    (property \"Datasheet\" \"\" (at %s %s 0)\n"
                "      (effects (font (size 1.27 1.27)) hide))\n"
                "%s\n"
                "    (instances (project %s (path \"/%s\" (reference %s) (unit 1))))\n"
                "  )" % (
                    q(part["lib_id"]), fmt(x), fmt(y), fmt(part["rot"]),
                    "no" if part["ref"].startswith("#") else "yes",
                    "no" if part["footprint"] == "" else "yes",
                    part["uuid"],
                    q(part["ref"]), fmt(ref_at[0]), fmt(ref_at[1]), hide_ref,
                    q(part["value"]), fmt(val_at[0]), fmt(val_at[1]),
                    " hide" if part["ref"].startswith("#") else "",
                    q(part["footprint"]), fmt(x), fmt(y),
                    fmt(x), fmt(y),
                    pin_lines,
                    q(PROJECT), SHEET, q(part["ref"]),
                ))
        chunks.extend("  " + item for item in self.draw)
        chunks.append(
            '  (text "TPS62840: R15 = 267 kOhm 1%% to GND selects 3.3 V. MODE and STOP tied to GND (power-save, switching enabled). EN follows VSYS." (at 25.4 385 0)\n'
            "    (effects (font (size 1.27 1.27)))\n"
            "    (uuid %s))" % uid())
        chunks.append(
            '  (text "Charger remains MCP73871-2CC, 4.20 V. PROG1 6.8k is about 147 mA. LowBAT is PA0. Crystal load caps are 18 pF." (at 25.4 395 0)\n'
            "    (effects (font (size 1.27 1.27)))\n"
            "    (uuid %s))" % uid())
        chunks.append('  (sheet_instances (path "/" (page "1")))')
        chunks.append(")")
        path.write_text("\n".join(chunks) + "\n", encoding="utf-8")


def load_footprint(fp_id):
    lib, name = fp_id.split(":", 1)
    path = FP / (lib + ".pretty") / (name + ".kicad_mod")
    text = path.read_text(encoding="utf-8", errors="replace")
    tree, _ = parse_sexp(text)
    return tree


def set_pad_nets(node, net_of_pin, net_ids):
    if not isinstance(node, list):
        return
    if node and node[0] == "pad":
        number = unq(node[1])
        net = net_of_pin.get(str(number))
        if net is not None:
            node.append(["net", str(net_ids[net]), q(net)])
    for item in node:
        if isinstance(item, list):
            set_pad_nets(item, net_of_pin, net_ids)


def rotate_pads(fp, deg):
    """KiCad stores pad orientation in board coordinates, not footprint-relative."""
    for pad in child(fp, "pad"):
        at = child(pad, "at")[0]
        angle = (float(at[3]) if len(at) > 3 else 0.0) + deg
        at[3:] = [fmt(angle % 360)]


def set_fp_text(node, kind, value):
    if not isinstance(node, list):
        return
    if len(node) >= 3 and node[0] == "fp_text" and node[1] == kind:
        node[2] = q(value)
    for item in node:
        if isinstance(item, list):
            set_fp_text(item, kind, value)


def write_pcb(path, parts):
    board_w, board_h = 86.0, 78.0
    # Passives sit next to the pins they serve.
    place = {
        # USB at the left edge; "PCB Edge" of the footprint is 2.67 mm from origin.
        "J2": (2.7, 16.0, 270),
        # Charger: VBUS enters from the top, VSYS leaves left towards the buck,
        # B+ leaves right towards the cell.
        "U5": (18.0, 21.0, 0),
        "C12": (11.0, 15.8, 90), "C14": (13.5, 18.3, 90),
        "R6": (13.8, 21.8, 0), "R7": (13.8, 23.3, 180), "R8": (13.8, 24.8, 180),
        "C11": (22.5, 19.5, 0), "R9": (22.5, 21.2, 0), "R10": (22.5, 22.7, 0),
        "LED2": (16.5, 26.0, 0), "R12": (16.5, 27.5, 0), "R11": (19.5, 26.0, 0),
        # 3.3 V buck: keep the VIN/GND/SW loop tight
        "U3": (9.0, 24.0, 180),
        "C9": (11.2, 24.5, 270), "L1": (5.6, 24.25, 180), "C10": (5.6, 27.3, 0),
        "R15": (8.0, 21.5, 0),
        # Cell protection
        "J1": (5.0, 44.0, 0),
        "C13": (6.0, 49.0, 0),
        "U4": (15.0, 43.0, 0),
        "R14": (11.0, 43.0, 90), "R13": (19.0, 43.0, 90), "C15": (20.5, 43.0, 90),
        "Q1": (15.0, 50.0, 180),
        # MCU with decoupling at each VDD/VSS pair
        "U1": (52.0, 40.0, 0),
        "C1": (49.5, 33.3, 0), "R1": (52.2, 33.3, 0),
        "C2": (58.2, 37.5, 270),
        "C3": (54.5, 46.3, 180), "R16": (52.2, 46.6, 90),
        "C4": (45.8, 41.0, 90), "C5": (45.8, 37.0, 90),
        "X1": (42.0, 39.5, 0), "C6": (40.9, 42.8, 0), "C7": (43.1, 36.2, 0),
        "R5": (46.0, 44.5, 0), "C8": (46.0, 46.0, 0), "SW1": (42.0, 49.0, 0),
        "LED1": (40.0, 32.0, 0), "R4": (40.0, 33.5, 0),
        "R2": (70.0, 34.0, 90), "R3": (71.5, 34.0, 90),
        # Headers. The NRF module hangs off the top edge.
        "H5": (43.0, 5.5, 0), "C16": (50.0, 6.5, 90), "C17": (51.8, 6.5, 90),
        "H2": (78.0, 8.0, 0),
        "H3": (78.0, 26.0, 0),
        "H4": (78.0, 40.0, 0),
        "H1": (16.0, 66.0, 0),
        "H6": (42.0, 66.0, 0),
    }

    nets = {}
    for part in parts:
        if not part["footprint"]:
            continue
        for net in part["pin_nets"].values():
            if net:
                nets.setdefault(net, None)
    ordered = ["GND", "+3V3"] + sorted(n for n in nets if n not in ("GND", "+3V3"))
    net_ids = {name: i + 1 for i, name in enumerate(ordered)}

    lines = [
        '(kicad_pcb (version 20221018) (generator pcbnew)',
        "  (general (thickness 1.6))",
        '  (paper "A4")',
        "  (layers",
        '    (0 "F.Cu" signal)',
        '    (1 "In1.Cu" power)',
        '    (2 "In2.Cu" power)',
        '    (31 "B.Cu" signal)',
        '    (32 "B.Adhes" user "B.Adhesive")',
        '    (33 "F.Adhes" user "F.Adhesive")',
        '    (34 "B.Paste" user)',
        '    (35 "F.Paste" user)',
        '    (36 "B.SilkS" user "B.Silkscreen")',
        '    (37 "F.SilkS" user "F.Silkscreen")',
        '    (38 "B.Mask" user)',
        '    (39 "F.Mask" user)',
        '    (40 "Dwgs.User" user "User.Drawings")',
        '    (41 "Cmts.User" user "User.Comments")',
        '    (42 "Eco1.User" user "User.Eco1")',
        '    (43 "Eco2.User" user "User.Eco2")',
        '    (44 "Edge.Cuts" user)',
        '    (45 "Margin" user)',
        '    (46 "B.CrtYd" user "B.Courtyard")',
        '    (47 "F.CrtYd" user "F.Courtyard")',
        '    (48 "B.Fab" user)',
        '    (49 "F.Fab" user)',
        "  )",
        "  (setup",
        "    (pad_to_mask_clearance 0)",
        "    (pcbplotparams (layerselection 0x00000000_00000000) (plotframeref false)",
        "      (usegerberextensions true) (usegerberattributes true) (creategerberjobfile true)",
        '      (outputdirectory "CAM/"))',
        "  )",
        '  (net 0 "")',
    ]
    for name, number in net_ids.items():
        lines.append("  (net %d %s)" % (number, q(name)))

    missing_place = []
    for part in parts:
        if not part["footprint"]:
            continue
        if part["ref"] not in place:
            missing_place.append(part["ref"])
            continue
        fp = load_footprint(part["footprint"])
        fp[1] = q(part["footprint"])
        fp[:] = [item for item in fp if not (
            isinstance(item, list) and item and item[0] in ("version", "generator", "tedit")
        )]
        x, y, deg = place[part["ref"]]
        insert = [
            ["tstamp", uid()],
            ["at", fmt(x), fmt(y), fmt(deg)],
            ["path", q("/" + part["uuid"])],
        ]
        for item in reversed(insert):
            fp.insert(2, item)
        set_fp_text(fp, "reference", part["ref"])
        set_fp_text(fp, "value", part["value"])
        set_pad_nets(fp, part["pin_nets"], net_ids)
        rotate_pads(fp, deg)
        lines.append(dump(fp, 1))
    if missing_place:
        raise SystemExit("no PCB position for " + ", ".join(missing_place))

    def edge(x1, y1, x2, y2):
        lines.append(
            "  (gr_line (start %s %s) (end %s %s)\n"
            "    (stroke (width 0.1) (type solid)) (layer \"Edge.Cuts\") (tstamp %s))" % (
                fmt(x1), fmt(y1), fmt(x2), fmt(y2), uid()))

    edge(0, 0, board_w, 0)
    edge(board_w, 0, board_w, board_h)
    edge(board_w, board_h, 0, board_h)
    edge(0, board_h, 0, 0)
    lines.append(
        '  (gr_text "OutdoorMeasureUnit  3V3 buck TPS62840  charger 4.20V" (at 43 2.2 0)\n'
        "    (layer \"F.SilkS\") (tstamp %s)\n"
        "    (effects (font (size 0.8 0.8) (thickness 0.12))))" % uid())
    lines.append(
        '  (gr_text "NRF module hangs off this edge" (at 43 3.6 0)\n'
        "    (layer \"F.SilkS\") (tstamp %s)\n"
        "    (effects (font (size 0.6 0.6) (thickness 0.1))))" % uid())
    # 4-layer stack: In1 solid GND, In2 solid +3V3. Signals on F.Cu and B.Cu.
    for net, layer in (("GND", "In1.Cu"), ("+3V3", "In2.Cu")):
        lines.append(
            "  (zone (net %d) (net_name %s) (layer \"%s\") (tstamp %s) (hatch edge 0.508)\n"
            "    (connect_pads yes (clearance 0.3))\n"
            "    (min_thickness 0.2) (filled_areas_thickness no)\n"
            "    (fill yes (thermal_gap 0.3) (thermal_bridge_width 0.4))\n"
            "    (polygon (pts (xy 0.4 0.4) (xy %s 0.4) (xy %s %s) (xy 0.4 %s))))" % (
                net_ids[net], q(net), layer, uid(), fmt(board_w - 0.4), fmt(board_w - 0.4),
                fmt(board_h - 0.4), fmt(board_h - 0.4)))
    lines.append(")")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return place


def netclass(name, track):
    return {
        "name": name, "track_width": track, "clearance": 0.2,
        "via_diameter": 0.6, "via_drill": 0.3,
        "microvia_diameter": 0.3, "microvia_drill": 0.1,
        "diff_pair_width": 0.2, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
        "bus_width": 12, "wire_width": 6, "line_style": 0,
        "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)",
    }


def main():
    sch = Schematic()

    def use(filename, name, lib):
        sym = lib_symbol(filename, name, lib)
        sch.add_lib(sym)
        return sym

    stm = use("MCU_ST_STM32F1.kicad_sym", "STM32F103C8Tx", "MCU_ST_STM32F1")
    chg = use("Battery_Management.kicad_sym", "MCP73871-2CC", "Battery_Management")
    usb = use("Connector.kicad_sym", "USB_B_Micro", "Connector")
    res = use("Device.kicad_sym", "R", "Device")
    cap = use("Device.kicad_sym", "C", "Device")
    ind = use("Device.kicad_sym", "L", "Device")
    led = use("Device.kicad_sym", "LED", "Device")
    xtal = use("Device.kicad_sym", "Crystal_GND24", "Device")
    sw = use("Switch.kicad_sym", "SW_Push", "Switch")
    for n, libn in (
        (2, "Conn_01x02"), (4, "Conn_01x04"), (5, "Conn_01x05"), (6, "Conn_01x06"),
    ):
        use("Connector_Generic.kicad_sym", libn, "Connector_Generic")
    conn2x4 = use("Connector_Generic.kicad_sym", "Conn_02x04_Odd_Even", "Connector_Generic")
    for pname in ("GND", "+3V3", "PWR_FLAG"):
        use("power.kicad_sym", pname, "power")

    tps = custom_symbol("TPS62840DLC", 7, [
        (1, "GND", -12.7, 5.08, 0, "power_in"),
        (2, "VIN", -12.7, 2.54, 0, "power_in"),
        (3, "MODE", -12.7, 0, 0, "input"),
        (4, "EN", -12.7, -2.54, 0, "input"),
        (5, "VSET", 12.7, 5.08, 180, "input"),
        (6, "STOP", 12.7, 2.54, 180, "input"),
        (7, "SW", 12.7, 0, 180, "passive"),
        (8, "VOS", 12.7, -2.54, 180, "passive"),
    ])
    fhdw = custom_symbol("FHDW01A", 7, [
        (1, "DOUT", -10.16, 5.08, 0, "output"),
        (2, "VM", -10.16, 2.54, 0, "input"),
        (3, "COUT", -10.16, 0, 0, "output"),
        (4, "NC", -10.16, -2.54, 0, "no_connect"),
        (5, "VDD", 10.16, 2.54, 180, "power_in"),
        (6, "VSS", 10.16, 0, 180, "power_in"),
    ])
    fet = custom_symbol("S8205A", 7, [
        (1, "D1/D2", -10.16, 7.62, 0, "passive"),
        (2, "S1", -10.16, 5.08, 0, "passive"),
        (3, "S1", -10.16, 2.54, 0, "passive"),
        (4, "G1", -10.16, 0, 0, "passive"),
        (5, "G2", 10.16, 7.62, 180, "passive"),
        (6, "S2", 10.16, 5.08, 180, "passive"),
        (7, "S2", 10.16, 2.54, 180, "passive"),
        (8, "D1/D2", 10.16, 0, 180, "passive"),
    ], ref_prefix="Q")
    for sym in (tps, fhdw, fet):
        sch.add_lib(sym)

    # Pin assignment follows the original EasyEDA schematic (rev 1.0).
    mcu_nets = {
        "VBAT": "+3V3", "VDDA": "+3V3", "VDD": "+3V3",
        "VSSA": "GND", "VSS": "GND",
        "PD0": "XTAL_XI", "PD1": "XTAL_XO", "NRST": "NRST",
        "PC13": "USER_LED", "PA0": "LOWBAT",
        "PA2": "NRF_CS", "PA3": "NRF_CE", "PA4": "NRF_IRQ",
        "PA5": "SPI1_SCK", "PA6": "SPI1_MISO", "PA7": "SPI1_MOSI",
        "PB2": "BOOT1", "BOOT0": "BOOT0",
        "PB6": "UART_TX", "PB7": "UART_RX",
        "PB10": "I2C_SCL", "PB11": "I2C_SDA",
        "PA13": "SWDIO", "PA14": "SWCLK",
    }
    if mcu_nets["PA0"] != "LOWBAT":
        raise SystemExit("LOWBAT must stay on PA0 (WKUP), as on the EasyEDA sheet")
    stm_pins = {}
    for pin in pins_of(stm):
        if pin["name"] in mcu_nets:
            stm_pins[pin["number"]] = mcu_nets[pin["name"]]
        elif pin["name"].startswith(("PA", "PB", "PC", "PD")):
            stm_pins[pin["number"]] = None
        else:
            raise SystemExit("unmapped MCU pin %s %s" % (pin["number"], pin["name"]))
    sch.add_part("MCU_ST_STM32F1:STM32F103C8Tx", "U1", "STM32F103C8Tx",
                 "Package_QFP:LQFP-48_7x7mm_P0.5mm", (320, 175), 0, stm_pins, stm)

    # MCP73871-2CC, 4.20 V. Pin numbers from the KiCad symbol.
    sch.add_part("Battery_Management:MCP73871-2CC", "U5", "MCP73871-2CCI/ML",
                 "Package_DFN_QFN:QFN-20-1EP_4x4mm_P0.5mm_EP2.5x2.5mm",
                 (95, 95), 0, {
                     "1": "VSYS", "20": "VSYS",
                     "2": "VPCC",
                     "3": "GND",          # SEL low: USB input limit
                     "4": "VBUS",         # PROG2 high with SEL low: 500 mA USB
                     "5": "THERM",
                     "6": None,           # PG open-drain, unused
                     "7": "CHARGE",
                     "8": "LOWBAT",
                     "9": "GND",          # ~TE low enables the safety timer
                     "10": "GND", "11": "GND", "21": "GND",
                     "12": "PROG3",
                     "13": "PROG1",
                     "14": "B+", "15": "B+", "16": "B+",
                     "17": "VBUS",        # CE high only while USB is present
                     "18": "VBUS", "19": "VBUS",
                 }, chg)

    sch.add_part("OutdoorMeasureUnit:TPS62840DLC", "U3", "TPS62840DLC",
                 "Package_DFN_QFN:Texas_VSON-HR-8_1.5x2mm_P0.5mm",
                 (95, 210), 0, {
                     "1": "GND", "2": "VSYS", "3": "GND", "4": "VSYS",
                     "5": "VSET", "6": "GND", "7": "SW", "8": "+3V3",
                 }, tps)

    sch.add_part("OutdoorMeasureUnit:FHDW01A", "U4", "FHDW01A",
                 "Package_TO_SOT_SMD:SOT-23-6",
                 (95, 310), 0, {
                     "1": "DOUT", "2": "VM", "3": "COUT", "4": None,
                     "5": "VDD_PROT", "6": "B-",
                 }, fhdw)
    sch.add_part("OutdoorMeasureUnit:S8205A", "Q1", "S8205A",
                 "Package_SO:TSSOP-8_4.4x3mm_P0.65mm",
                 (175, 310), 0, {
                     "1": "DCOM", "8": "DCOM",
                     "2": "GND", "3": "GND",
                     "4": "COUT", "5": "DOUT",
                     "6": "B-", "7": "B-",
                 }, fet)

    sch.add_part("Connector:USB_B_Micro", "J2", "USB_Micro-B",
                 "Connector_USB:USB_Micro-B_Molex_47346-0001",
                 (40, 95), 0, {
                     "1": "VBUS", "2": None, "3": None, "4": None,
                     "5": "GND", "6": "GND",
                 }, usb)

    def conn(ref, value, symbol, footprint, pins, at):
        sym = sch.cached("Connector_Generic:" + symbol)
        sch.add_part("Connector_Generic:" + symbol, ref, value, footprint, at, 0,
                     {str(i + 1): net for i, net in enumerate(pins)}, sym)

    conn("J1", "BAT", "Conn_01x02",
         "Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical",
         ["B+", "B-"], (40, 310))
    # Header pinouts match the sensor modules in the original schematic.
    conn("H1", "UART", "Conn_01x02",
         "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
         ["UART_TX", "UART_RX"], (510, 300))
    conn("H6", "DEBUG", "Conn_01x04",
         "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
         ["GND", "SWCLK", "SWDIO", "NRST"], (460, 300))
    conn("H3", "Si7021", "Conn_01x04",
         "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
         ["GND", "+3V3", "I2C_SDA", "I2C_SCL"], (510, 190))
    conn("H2", "TSL2561", "Conn_01x05",
         "Connector_PinHeader_2.54mm:PinHeader_1x05_P2.54mm_Vertical",
         ["GND", "I2C_SDA", "I2C_SCL", "GND", "+3V3"], (510, 120))
    conn("H4", "BMP280", "Conn_01x06",
         "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
         ["+3V3", "GND", "I2C_SCL", "I2C_SDA", None, None], (510, 250))
    sch.add_part("Connector_Generic:Conn_02x04_Odd_Even", "H5", "NRF24L01+",
                 "Connector_PinHeader_2.54mm:PinHeader_2x04_P2.54mm_Vertical",
                 (490, 55), 0, {
                     "1": "GND", "2": "+3V3", "3": "NRF_CE", "4": "NRF_CS",
                     "5": "SPI1_SCK", "6": "SPI1_MOSI", "7": "SPI1_MISO", "8": "NRF_IRQ",
                 }, conn2x4)

    def two(lib_id, sym, ref, value, footprint, n1, n2, at):
        sch.add_part(lib_id, ref, value, footprint, at, 0, {"1": n1, "2": n2}, sym)

    # Passives sit in the same block as the IC they belong to (A2 sheet).
    two("Device:R", res, "R9", "6.8k", R0402, "PROG1", "GND", (155, 55))
    two("Device:R", res, "R10", "100k", R0402, "PROG3", "GND", (155, 75))
    two("Device:R", res, "R6", "330k", R0402, "VBUS", "VPCC", (40, 55))
    two("Device:R", res, "R7", "110k", R0402, "VPCC", "GND", (40, 130))
    two("Device:R", res, "R8", "10k NTC", R0402, "THERM", "GND", (40, 150))
    two("Device:R", res, "R13", "100", R0402, "B+", "VDD_PROT", (40, 270))
    two("Device:R", res, "R14", "1k", R0402, "VM", "GND", (155, 340))
    two("Device:R", res, "R15", "267k 1%", R0402, "VSET", "GND", (40, 210))
    two("Device:L", ind, "L1", "2.2uH", L2016, "SW", "+3V3", (155, 210))
    two("Device:C", cap, "C9", "4.7u", C0603, "VSYS", "GND", (40, 190))
    two("Device:C", cap, "C10", "10u", C0603, "+3V3", "GND", (155, 230))
    two("Device:C", cap, "C14", "4.7u", C0603, "VSYS", "GND", (155, 115))
    two("Device:C", cap, "C12", "4.7u", C0603, "VBUS", "GND", (40, 35))
    two("Device:C", cap, "C11", "4.7u", C0603, "B+", "GND", (155, 135))
    two("Device:C", cap, "C13", "4.7u", C0603, "B+", "B-", (40, 350))
    two("Device:C", cap, "C15", "100n", C0402, "VDD_PROT", "B-", (155, 270))
    two("Device:C", cap, "C16", "10u", C0603, "+3V3", "GND", (545, 45))
    two("Device:C", cap, "C17", "100n", C0402, "+3V3", "GND", (545, 65))

    two("Device:R", res, "R1", "10k", R0402, "BOOT0", "GND", (250, 155))
    two("Device:R", res, "R16", "10k", R0402, "BOOT1", "GND", (385, 130))
    two("Device:R", res, "R5", "10k", R0402, "+3V3", "NRST", (250, 215))
    two("Device:C", cap, "C8", "100n", C0402, "NRST", "GND", (250, 235))
    two("Device:R", res, "R2", "10k", R0402, "+3V3", "I2C_SCL", (385, 195))
    two("Device:R", res, "R3", "10k", R0402, "+3V3", "I2C_SDA", (385, 215))
    two("Switch:SW_Push", sw, "SW1", "RESET",
        "Button_Switch_SMD:SW_SPST_B3U-1000P", "NRST", "GND", (220, 215))
    two("Device:R", res, "R11", "10k", R0402, "+3V3", "LOWBAT", (250, 175))
    two("Device:R", res, "R4", "510", R0402, "+3V3", "LED1_A", (250, 50))
    two("Device:LED", led, "LED1", "Yellow", LED0402, "USER_LED", "LED1_A", (275, 50))
    # Charge LED is fed from VBUS, so it never draws from the cell.
    two("Device:R", res, "R12", "1k", R0402, "VBUS", "LED2_A", (40, 170))
    two("Device:LED", led, "LED2", "Charge", LED0402, "CHARGE", "LED2_A", (65, 170))
    sch.add_part("Device:Crystal_GND24", "X1", "8MHz", XTAL, (250, 80), 0, {
        "1": "XTAL_XI", "2": "GND", "3": "XTAL_XO", "4": "GND",
    }, xtal)
    two("Device:C", cap, "C6", "18p", C0402, "XTAL_XI", "GND", (230, 105))
    two("Device:C", cap, "C7", "18p", C0402, "XTAL_XO", "GND", (270, 105))
    for i, ref in enumerate(("C1", "C2", "C3", "C4")):
        two("Device:C", cap, ref, "100n", C0402, "+3V3", "GND", (385, 50 + i * 18))
    two("Device:C", cap, "C5", "4.7u", C0603, "+3V3", "GND", (420, 50))

    # PWR_FLAG marks the buck output as the source of +3V3. B- is the cell
    # negative and is not the system ground; the flag only satisfies the
    # protection IC power pin.
    sch_path = ROOT / (PROJECT + ".kicad_sch")
    # Inject flags during emit by wrapping write.
    original_emit = sch.emit_connections

    def emit_with_flags():
        original_emit()
        # One power-out flag on the buck sense net and one on the cell negative.
        for net, at in (("+3V3", (175.0, 230.0)), ("B-", (200.0, 330.0)),
                        ("VDD_PROT", (175.0, 270.0))):
            sch.place_power("PWR_FLAG", at, 0)
            sch.draw.append(
                "(label %s (at %s %s 0)\n"
                "  (effects (font (size 1.27 1.27)) (justify left bottom))\n"
                "  (uuid %s))" % (q(net), fmt(at[0]), fmt(at[1]), uid()))

    sch.emit_connections = emit_with_flags
    sch.write(sch_path)
    pcb_path = ROOT / (PROJECT + ".kicad_pcb")
    write_pcb(pcb_path, sch.parts)
    pro = ROOT / (PROJECT + ".kicad_pro")
    shutil.copy(Path("/usr/share/kicad/template/kicad.kicad_pro"), pro)
    text = pro.read_text(encoding="utf-8").replace("kicad.kicad_pro", pro.name)
    data = json.loads(text)
    # Power tracks stay at 0.3 mm: wider ones cannot leave 0.5 mm pitch QFN/VSON pads.
    # "Plane" nets reach In1/In2 through fanout vias placed by route_pcb.py.
    data["net_settings"] = {
        "classes": [netclass("Default", 0.2), netclass("Power", 0.3), netclass("Plane", 0.3)],
        "meta": {"version": 3},
        "net_colors": None,
        "netclass_assignments": None,
        "netclass_patterns": [
            {"netclass": "Power", "pattern": net}
            for net in ("VBUS", "VSYS", "B+", "B-", "DCOM", "SW")
        ] + [{"netclass": "Plane", "pattern": net} for net in ("GND", "+3V3")],
    }
    pro.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print("wrote", sch_path)
    print("wrote", pcb_path)


if __name__ == "__main__":
    main()
