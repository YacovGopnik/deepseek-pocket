#!/usr/bin/env python3
"""Плата-основа «DeepSeek в кармане»: генерирует проект KiCad (KiCad 8).

Готовые модули (ESP32-C3 Super Mini, экран, джойстик, зарядка TP4056) впаиваются в плату или
подключаются к ней короткими проводами; на плате - питание, делитель батареи, пищалка,
ИК-светодиод и фонарик.

Запуск: python3 gen_pcb.py  ->  deepseek-pocket.kicad_pcb, deepseek-pocket.kicad_pro, preview.png
Трассировка - простой волновой алгоритм по сетке 0,635 мм на двух слоях; в конце своя проверка
зазоров и связности. Перед заказом обязательно запустите DRC в KiCad.
"""
import heapq
import json
import math
import os
import sys
import uuid

G = 0.635                      # шаг сетки, мм
W, H = 60.96, 101.6            # размер платы, мм
NX, NY = int(round(W / G)), int(round(H / G))
TRACK = 0.4                    # ширина дорожки
CLEAR = 0.2                    # зазор
VIA_D, VIA_DRILL = 0.8, 0.4
EDGE_KEEP = 1.0                # дорожки не ближе к краю
LAYERS = ("F.Cu", "B.Cu")
HERE = os.path.dirname(os.path.abspath(__file__))


def uid():
    return str(uuid.uuid4())


# ---------------------------------------------------------------- компоненты
class Pad:
    def __init__(self, num, x, y, net, shape="circle", size=1.6, drill=1.0, label=None):
        self.num, self.x, self.y, self.net = num, x, y, net
        self.shape, self.size, self.drill, self.label = shape, size, drill, label


class Part:
    def __init__(self, ref, value, x, y, desc=""):
        self.ref, self.value, self.x, self.y, self.desc = ref, value, x, y, desc
        self.pads = []
        self.silk = []          # (x1, y1, x2, y2) в абсолютных координатах
        self.texts = []         # (text, x, y, size)
        self.holes = []         # (x, y, d) - крепёжные отверстия без меди

    def pad(self, num, x, y, net, **kw):
        p = Pad(num, x, y, net, **kw)
        self.pads.append(p)
        return p


parts = []


def header_row(ref, value, x0, y0, nets, labels, dx=2.54, dy=0.0, label_side=-1.6, drill=1.0, size=1.7):
    """Ряд штырей: первый - квадратный."""
    p = Part(ref, value, x0, y0)
    for i, (n, lab) in enumerate(zip(nets, labels)):
        x, y = x0 + i * dx, y0 + i * dy
        p.pad(str(i + 1), x, y, n, shape="rect" if i == 0 else "circle", size=size, drill=drill, label=lab)
        if lab:
            if dy == 0:
                p.texts.append((lab, x, y + label_side, 0.7))
            else:
                p.texts.append((lab, x + label_side * 1.6, y, 0.7))
    xs = [x0 + i * dx for i in range(len(nets))]
    ys = [y0 + i * dy for i in range(len(nets))]
    m = 1.27
    box(p, min(xs) - m, min(ys) - m, max(xs) + m, max(ys) + m)
    parts.append(p)
    return p


def box(p, x1, y1, x2, y2):
    p.silk += [(x1, y1, x2, y1), (x2, y1, x2, y2), (x2, y2, x1, y2), (x1, y2, x1, y1)]


def two_pin(ref, value, x1, y1, x2, y2, n1, n2, drill=0.8, size=1.6, l1=None, l2=None, body=None):
    p = Part(ref, value, (x1 + x2) / 2, (y1 + y2) / 2)
    p.pad("1", x1, y1, n1, shape="rect" if l1 else "circle", drill=drill, size=size, label=l1)
    p.pad("2", x2, y2, n2, drill=drill, size=size, label=l2)
    if body:
        bx, by = body
        cx, cy = (x1 + x2) / 2, (y1 + y2) / 2
        box(p, cx - bx / 2, cy - by / 2, cx + bx / 2, cy + by / 2)
        p.silk += [(x1 + 0.9, y1, cx - bx / 2, cy), (cx + bx / 2, cy, x2 - 0.9, y2)]
    parts.append(p)
    return p


def to92(ref, value, x0, y0, nE, nB, nC):
    p = Part(ref, value, x0 + 2.54, y0)
    for i, (n, lab) in enumerate(((nE, "E"), (nB, "B"), (nC, "C"))):
        p.pad(str(i + 1), x0 + i * 2.54, y0, n, shape="rect" if i == 0 else "circle", size=1.4, drill=0.75, label=lab)
        p.texts.append((lab, x0 + i * 2.54, y0 + 1.8, 0.6))
    # плоская сторона - вниз (к подписям E B C)
    p.silk += [(x0 - 1.2, y0 + 1.0, x0 + 6.28, y0 + 1.0)]
    for k in range(9):
        a1, a2 = math.pi + k * math.pi / 9, math.pi + (k + 1) * math.pi / 9
        r = 3.1
        cx = x0 + 2.54
        p.silk.append((cx + r * math.cos(a1), y0 + 1.0 + r * math.sin(a1) * 0.9, cx + r * math.cos(a2), y0 + 1.0 + r * math.sin(a2) * 0.9))
    parts.append(p)
    return p


# ---- ESP32-C3 Super Mini (вид сверху, USB смотрит вниз к краю платы) ----
# Сверху вниз при USB вверху: левый ряд 5V G 3.3 4 3 2 1 0, правый 5 6 7 8 9 10 20 21.
# Развёрнут на 180°: правый ряд (x=22.86) снизу вверх 5V G 3.3 4 3 2 1 0; левый (x=7.62) снизу вверх 5 6 7 8 9 10 20 21.
U1 = Part("U1", "ESP32-C3 Super Mini", 15.24, 88.9, "модуль ESP32-C3 Super Mini, USB к нижнему краю")
right_col = [("+5V", "5V"), ("GND", "G"), ("+3V3", "3.3"), ("SCK", "4"), ("BATS", "3"), ("LED", "2"), ("JY", "1"), ("JX", "0")]
left_col = [("DC", "5"), ("MOSI", "6"), ("JSW", "7"), ("IR", "8"), ("NC9", "9"), ("BLK", "10"), ("BUZ", "20"), ("RES", "21")]
for i in range(8):
    y = 97.79 - i * 2.54
    n, lab = right_col[i]
    U1.pad("R%d" % (i + 1), 22.86, y, n, shape="rect" if i == 0 else "circle", size=1.7, drill=1.0, label=lab)
    U1.texts.append((lab, 22.86 - 2.1, y, 0.7))
    n, lab = left_col[i]
    U1.pad("L%d" % (i + 1), 7.62, y, None if n == "NC9" else n, size=1.7, drill=1.0, label=lab)
    U1.texts.append((lab, 7.62 + 2.1, y, 0.7))
box(U1, 6.24, 78.7, 24.24, 101.2)                       # контур модуля 18 x 22,5 мм
U1.silk += [(11.74, 101.2, 11.74, 98.6), (18.74, 101.2, 18.74, 98.6), (11.74, 98.6, 18.74, 98.6)]   # USB
U1.texts.append(("USB", 15.24, 99.9, 0.8))
U1.texts.append(("ESP32-C3 SuperMini", 15.24, 77.6, 0.8))
parts.append(U1)

# ---- разъёмы модулей ----
header_row("J1", "Дисплей ST7789", 22.86, 5.08, ["GND", "+3V3", "SCK", "MOSI", "RES", "DC", "BLK"],
           ["GND", "VCC", "SCL", "SDA", "RES", "DC", "BLK"], label_side=2.0)
parts[-1].texts.append(("ДИСПЛЕЙ", 30.48, 1.9, 0.8))
header_row("J2", "Джойстик KY-023", 35.56, 73.66, ["GND", "+3V3", "JX", "JY", "JSW"],
           ["GND", "+5V", "VRx", "VRy", "SW"], label_side=2.0)
parts[-1].texts.append(("ДЖОЙСТИК (+5V = 3,3 В)", 40.64, 70.6, 0.7))
header_row("J3", "Аккумулятор", 48.26, 13.97, ["BATP", "BATN"], ["+", "-"], label_side=2.0)
parts[-1].texts.append(("АККУМ", 49.53, 10.9, 0.8))
header_row("J4", "TP4056", 45.72, 24.13, ["BATP", "BATN", "VOUT", "GND"], ["B+", "B-", "OUT+", "OUT-"], label_side=2.0)
parts[-1].texts.append(("TP4056 (с защитой)", 49.53, 21.0, 0.7))
header_row("SW1", "Выключатель", 48.26, 33.02, ["VOUT", "VSW", None], ["", "", ""], label_side=2.0)
parts[-1].texts.append(("ВКЛ", 50.8, 29.9, 0.8))

# ---- питание ----
two_pin("D1", "1N5819", 40.64, 40.64, 50.8, 40.64, "VSW", "+5V", drill=1.0, l1=None, body=(5.2, 2.7))
parts[-1].texts += [("D1 1N5819", 45.72, 38.4, 0.7), ("|", 48.0, 40.64, 1.2)]
two_pin("JP1", "Перемычка", 40.64, 45.72, 43.18, 45.72, "VSW", "+5V", drill=0.8)
parts[-1].texts.append(("JP1 вместо D1", 47.0, 45.72, 0.6))
two_pin("R1", "100k", 35.56, 50.8, 45.72, 50.8, "VSW", "BATS", body=(6.3, 2.5))
parts[-1].texts.append(("R1 100k", 40.64, 52.6, 0.7))
two_pin("R2", "100k", 35.56, 55.88, 45.72, 55.88, "BATS", "GND", body=(6.3, 2.5))
parts[-1].texts.append(("R2 100k", 40.64, 57.7, 0.7))

# ---- фонарик и ИК (светодиоды у верхнего края - выгнуть наружу) ----
two_pin("D3", "LED белый 5мм", 7.62, 10.16, 10.16, 10.16, "LEDA", "LEDK", drill=0.9)
parts[-1].texts.append(("ФОНАРЬ", 8.89, 7.8, 0.7))
parts[-1].texts.append(("+", 6.3, 10.16, 0.7))
two_pin("D2", "ИК-светодиод 5мм 940нм", 15.24, 10.16, 17.78, 10.16, "IRA", "IRK", drill=0.9)
parts[-1].texts.append(("ИК", 16.51, 7.8, 0.7))
parts[-1].texts.append(("+", 13.9, 10.16, 0.7))
to92("Q2", "S9014", 5.08, 17.78, "GND", "LEDB", "LEDK")
parts[-1].texts.append(("Q2", 7.62, 14.9, 0.7))
to92("Q1", "S9014", 13.97, 17.78, "IRA", "IR", "+5V")
parts[-1].texts.append(("Q1", 16.51, 14.9, 0.7))
two_pin("R5", "47", 5.08, 25.4, 15.24, 25.4, "+5V", "LEDA", body=(6.3, 2.5))
parts[-1].texts.append(("R5 47", 10.16, 27.2, 0.7))
two_pin("R3", "47", 5.08, 30.48, 15.24, 30.48, "IRK", "GND", body=(6.3, 2.5))
parts[-1].texts.append(("R3 47", 10.16, 32.3, 0.7))
two_pin("R4", "22k", 5.08, 35.56, 15.24, 35.56, "LED", "LEDB", body=(6.3, 2.5))
parts[-1].texts.append(("R4 22k", 10.16, 37.4, 0.7))
two_pin("R6", "4.7k", 5.08, 40.64, 15.24, 40.64, "LED", "+3V3", body=(6.3, 2.5))
parts[-1].texts.append(("R6 4,7k", 10.16, 42.5, 0.7))

# ---- пищалка 12 мм, шаг 7,62 ----
bz = two_pin("BZ1", "Пищалка 12мм", 7.62, 50.8, 15.24, 50.8, "BUZ", "GND", drill=0.9, l1="+")
bz.texts.append(("+", 7.62, 48.4, 0.9))
bz.texts.append(("BZ1", 11.43, 57.9, 0.7))
for k in range(24):
    a1, a2 = k * math.pi / 12, (k + 1) * math.pi / 12
    bz.silk.append((11.43 + 6.1 * math.cos(a1), 50.8 + 6.1 * math.sin(a1), 11.43 + 6.1 * math.cos(a2), 50.8 + 6.1 * math.sin(a2)))

# ---- крепёжные отверстия M3 ----
MH = Part("H1", "M3", 0, 0)
for (x, y) in ((3.81, 3.81), (57.15, 3.81), (3.81, 97.79), (57.15, 97.79)):
    MH.holes.append((x, y, 3.2))
parts.append(MH)

# ---- подписи на плате ----
board_texts = [("DeepSeek в кармане", 40.64, 88.9, 1.2), ("github.com/YacovGopnik/deepseek-pocket", 40.64, 92.0, 0.7),
               ("v1", 40.64, 95.0, 0.8)]

# ---------------------------------------------------------------- сети
nets = []
for p in parts:
    for pd in p.pads:
        if pd.net and pd.net not in nets:
            nets.append(pd.net)
nets.sort(key=lambda n: (n != "GND", n))
NET_ID = {n: i + 1 for i, n in enumerate(nets)}
all_pads = [(p, pd) for p in parts for pd in p.pads]


# ---------------------------------------------------------------- трассировка
def pad_dist(pd, x, y):
    """Расстояние от точки до края площадки (внутри - отрицательное или 0)."""
    if pd.shape == "rect":
        h = pd.size / 2
        dx, dy = max(abs(x - pd.x) - h, 0), max(abs(y - pd.y) - h, 0)
        return math.hypot(dx, dy)
    return math.hypot(x - pd.x, y - pd.y) - pd.size / 2


def seg_pad_dist(x1, y1, x2, y2, pd):
    n = max(1, int(math.hypot(x2 - x1, y2 - y1) / 0.02))
    return min(pad_dist(pd, x1 + (x2 - x1) * k / n, y1 + (y2 - y1) * k / n) for k in range(n + 1))


def cell(x, y):
    i, j = x / G, y / G
    assert abs(i - round(i)) < 1e-6 and abs(j - round(j)) < 1e-6, (x, y)
    return int(round(i)), int(round(j))


def route_all(order):
    owner = [[[0] * NX for _ in range(NY)] for _ in range(2)]
    BL = -1
    for L in range(2):
        for j in range(NY):
            for i in range(NX):
                x, y = i * G, j * G
                if x < EDGE_KEEP or y < EDGE_KEEP or x > W - EDGE_KEEP or y > H - EDGE_KEEP:
                    owner[L][j][i] = BL
    for p in parts:
        for (hx, hy, d) in p.holes:
            rr = d / 2 + CLEAR + TRACK / 2 + 0.3
            for j in range(NY):
                for i in range(NX):
                    if math.hypot(i * G - hx, j * G - hy) < rr:
                        owner[0][j][i] = owner[1][j][i] = BL
    for (p, pd) in all_pads:
        rr = CLEAR + TRACK / 2 + 1e-6
        nid = NET_ID.get(pd.net, BL) if pd.net else BL
        ci, cj = cell(pd.x, pd.y)
        R = int((pd.size + rr) / G) + 1
        for j in range(cj - R, cj + R + 1):
            for i in range(ci - R, ci + R + 1):
                if 0 <= i < NX and 0 <= j < NY and pad_dist(pd, i * G, j * G) < rr:
                    for L in range(2):
                        cur = owner[L][j][i]
                        if cur == 0:
                            owner[L][j][i] = nid
                        elif cur != nid:
                            owner[L][j][i] = BL
    vias = []          # (x, y, net)
    segs = []          # (x1, y1, x2, y2, layer, net)
    DIRS = ((1, 0), (-1, 0), (0, 1), (0, -1))

    def via_ok(i, j, nid):
        x, y = i * G, j * G
        for (vx, vy, vn) in vias:
            if vn != nid and math.hypot(vx - x, vy - y) < VIA_D + CLEAR + 1e-6:
                return False
        for (p, pd) in all_pads:
            if NET_ID.get(pd.net) != nid and pad_dist(pd, x, y) < VIA_D / 2 + CLEAR + 1e-6:
                return False
        for L in range(2):
            for (di, dj) in ((0, 0),) + DIRS:
                ii, jj = i + di, j + dj
                if not (0 <= ii < NX and 0 <= jj < NY):
                    return False
                o = owner[L][jj][ii]
                if o != 0 and o != nid:
                    return False
        return True

    for net in order:
        nid = NET_ID[net]
        terms = [cell(pd.x, pd.y) for (p, pd) in all_pads if pd.net == net]
        if len(terms) < 2:
            continue
        tree = {terms[0]}
        rest = terms[1:]
        # ближние сначала
        rest.sort(key=lambda t: min(abs(t[0] - a) + abs(t[1] - b) for (a, b) in tree))
        for tgt in rest:
            if tgt in tree:
                continue
            # Дейкстра: состояние (i, j, L, dir)
            dist = {}
            prev = {}
            pq = []
            for (a, b) in tree:
                for L in range(2):
                    if owner[L][b][a] in (nid,):
                        s = (a, b, L, -1)
                        dist[s] = 0
                        heapq.heappush(pq, (0, s))
            found = None
            while pq:
                d, s = heapq.heappop(pq)
                if dist.get(s, 1e18) < d:
                    continue
                i, j, L, dr = s
                if (i, j) == tgt:
                    found = s
                    break
                for k, (di, dj) in enumerate(DIRS):
                    ii, jj = i + di, j + dj
                    if not (0 <= ii < NX and 0 <= jj < NY):
                        continue
                    o = owner[L][jj][ii]
                    if o != 0 and o != nid:
                        continue
                    nd = d + 1 + (0.6 if (dr != -1 and dr != k) else 0) + (0.15 if L == 1 else 0)
                    ns = (ii, jj, L, k)
                    if nd < dist.get(ns, 1e18):
                        dist[ns] = nd
                        prev[ns] = s
                        heapq.heappush(pq, (nd, ns))
                # переход на другой слой
                o2 = owner[1 - L][j][i]
                if o2 in (0, nid) and via_ok(i, j, nid):
                    nd = d + 12
                    ns = (i, j, 1 - L, -1)
                    if nd < dist.get(ns, 1e18):
                        dist[ns] = nd
                        prev[ns] = s
                        heapq.heappush(pq, (nd, ns))
            if not found:
                return None, net
            # восстановить путь
            path = [found]
            while path[-1] in prev:
                path.append(prev[path[-1]])
            path.reverse()
            for k in range(len(path)):
                i, j, L, _ = path[k]
                owner[L][j][i] = nid
                tree.add((i, j))
                if k > 0:
                    pi, pj, pL, _ = path[k - 1]
                    if pL != L:
                        vias.append((i * G, j * G, nid))
                        for LL in range(2):
                            for (di, dj) in DIRS:
                                if owner[LL][j + dj][i + di] == 0:
                                    owner[LL][j + dj][i + di] = nid
                    else:
                        segs.append((pi * G, pj * G, i * G, j * G, L, nid))
    return (segs, vias), None


def merge_segments(segs):
    """Соединить подряд идущие отрезки одного направления."""
    out = []
    for s in segs:
        if out:
            x1, y1, x2, y2, L, n = out[-1]
            a1, b1, a2, b2, L2, n2 = s
            if L == L2 and n == n2 and abs(x2 - a1) < 1e-6 and abs(y2 - b1) < 1e-6:
                d1 = (round((x2 - x1) / G), round((y2 - y1) / G))
                d2 = (round((a2 - a1) / G), round((b2 - b1) / G))
                n1 = (d1[0] and d1[0] // abs(d1[0]), d1[1] and d1[1] // abs(d1[1]))
                n2_ = (d2[0] and d2[0] // abs(d2[0]), d2[1] and d2[1] // abs(d2[1]))
                if n1 == n2_:
                    out[-1] = (x1, y1, a2, b2, L, n)
                    continue
        out.append(s)
    return out


# ---------------------------------------------------------------- проверка
def seg_point_dist(x1, y1, x2, y2, px, py):
    dx, dy = x2 - x1, y2 - y1
    L2 = dx * dx + dy * dy
    t = 0 if L2 == 0 else max(0, min(1, ((px - x1) * dx + (py - y1) * dy) / L2))
    return math.hypot(x1 + t * dx - px, y1 + t * dy - py)


def seg_seg_dist(a, b):
    x1, y1, x2, y2 = a
    x3, y3, x4, y4 = b

    def ccw(ax, ay, bx, by, cx, cy):
        return (cy - ay) * (bx - ax) - (by - ay) * (cx - ax)
    d1, d2 = ccw(x3, y3, x4, y4, x1, y1), ccw(x3, y3, x4, y4, x2, y2)
    d3, d4 = ccw(x1, y1, x2, y2, x3, y3), ccw(x1, y1, x2, y2, x4, y4)
    if ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)):
        return 0.0
    return min(seg_point_dist(x1, y1, x2, y2, x3, y3), seg_point_dist(x1, y1, x2, y2, x4, y4),
               seg_point_dist(x3, y3, x4, y4, x1, y1), seg_point_dist(x3, y3, x4, y4, x2, y2))


def check(segs, vias):
    errs = []
    eps = 1e-6
    # медь: (тип, геометрия, радиус, net, слои)
    items = []
    for (x1, y1, x2, y2, L, n) in segs:
        items.append(("seg", (x1, y1, x2, y2), TRACK / 2, n, {L}))
    for (x, y, n) in vias:
        items.append(("via", (x, y, x, y), VIA_D / 2, n, {0, 1}))
    for (p, pd) in all_pads:
        items.append(("pad " + p.ref + "." + pd.num, (pd.x, pd.y, pd.x, pd.y), 0, NET_ID.get(pd.net, -100 - id(pd)), {0, 1}, pd))
    for a in range(len(items)):
        for b in range(a + 1, len(items)):
            A, B = items[a], items[b]
            if A[3] == B[3] or not (A[4] & B[4]):
                continue
            pa, pb = (A[5] if len(A) > 5 else None), (B[5] if len(B) > 5 else None)
            if pa and pb:
                d = math.hypot(pa.x - pb.x, pa.y - pb.y) - pa.size / 2 * (1.42 if pa.shape == "rect" else 1) - pb.size / 2 * (1.42 if pb.shape == "rect" else 1)
            elif pa or pb:
                pd_, other = (pa, B) if pa else (pb, A)
                if seg_seg_dist(other[1], (pd_.x, pd_.y, pd_.x, pd_.y)) - other[2] - pd_.size > 1.0:
                    continue
                d = seg_pad_dist(*other[1], pd_) - other[2]
            else:
                d = seg_seg_dist(A[1], B[1]) - A[2] - B[2]
            if d < CLEAR - eps:
                if A[0].startswith("pad") and B[0].startswith("pad") and A[0].split(".")[0] == B[0].split(".")[0]:
                    continue          # площадки одной детали - задано корпусом
                errs.append("зазор %.3f: %s / %s" % (d, A[0], B[0]))
    for (x1, y1, x2, y2, L, n) in segs:
        for (x, y) in ((x1, y1), (x2, y2)):
            if x < 0.5 or y < 0.5 or x > W - 0.5 or y > H - 0.5:
                errs.append("у края: %.2f %.2f" % (x, y))
    for p in parts:
        for (hx, hy, d) in p.holes:
            for it in items:
                r_ = it[5].size / 2 * 1.42 if len(it) > 5 else it[2]
                if seg_seg_dist(it[1], (hx, hy, hx, hy)) - r_ - d / 2 < CLEAR:
                    errs.append("у отверстия: " + it[0])
    # связность
    for net in nets:
        nid = NET_ID[net]
        nodes = []
        for (p, pd) in all_pads:
            if pd.net == net:
                nodes.append(("pad", pd.x, pd.y, {0, 1}))
        for (x1, y1, x2, y2, L, n) in segs:
            if n == nid:
                nodes.append(("seg", (x1, y1, x2, y2), L))
        for (x, y, n) in vias:
            if n == nid:
                nodes.append(("via", x, y, {0, 1}))
        par = list(range(len(nodes)))

        def find(a):
            while par[a] != a:
                par[a] = par[par[a]]
                a = par[a]
            return a

        def geo(nd):
            if nd[0] == "seg":
                return nd[1], {nd[2]}
            return (nd[1], nd[2], nd[1], nd[2]), nd[3]
        for a in range(len(nodes)):
            for b in range(a + 1, len(nodes)):
                ga, la = geo(nodes[a])
                gb, lb = geo(nodes[b])
                if la & lb and seg_seg_dist(ga, gb) < 1e-6:
                    par[find(a)] = find(b)
        pads_idx = [k for k, nd in enumerate(nodes) if nd[0] == "pad"]
        roots = {find(k) for k in pads_idx}
        if len(roots) > 1:
            errs.append("не соединена сеть " + net)
    return errs


# ---------------------------------------------------------------- KiCad
def q(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_pcb(segs, vias, path):
    o = []
    o.append('(kicad_pcb (version 20240108) (generator "pcbnew") (generator_version "8.0")')
    o.append('  (general (thickness 1.6) (legacy_teardrops no))')
    o.append('  (paper "A4")')
    o.append('''  (layers
    (0 "F.Cu" signal)
    (31 "B.Cu" signal)
    (32 "B.Adhes" user "B.Adhesive")
    (33 "F.Adhes" user "F.Adhesive")
    (34 "B.Paste" user)
    (35 "F.Paste" user)
    (36 "B.SilkS" user "B.Silkscreen")
    (37 "F.SilkS" user "F.Silkscreen")
    (38 "B.Mask" user)
    (39 "F.Mask" user)
    (40 "Dwgs.User" user "User.Drawings")
    (41 "Cmts.User" user "User.Comments")
    (42 "Eco1.User" user "User.Eco1")
    (43 "Eco2.User" user "User.Eco2")
    (44 "Edge.Cuts" user)
    (45 "Margin" user)
    (46 "B.CrtYd" user "B.Courtyard")
    (47 "F.CrtYd" user "F.Courtyard")
    (48 "B.Fab" user)
    (49 "F.Fab" user)
  )''')
    o.append('  (setup (pad_to_mask_clearance 0) (allow_soldermask_bridges_in_footprints no))')
    o.append('  (net 0 "")')
    for n in nets:
        o.append('  (net %d %s)' % (NET_ID[n], q(n)))

    def net_ref(n):
        return '(net %d %s)' % (NET_ID[n], q(n)) if n else ''

    for p in parts:
        o.append('  (footprint %s (layer "F.Cu") (uuid %s) (at %.4f %.4f)' % (q("dspocket:" + p.ref), q(uid()), p.x, p.y))
        o.append('    (property "Reference" %s (at 0 0 0) (layer "F.Fab") (uuid %s) (effects (font (size 1 1) (thickness 0.15))))' % (q(p.ref), q(uid())))
        o.append('    (property "Value" %s (at 0 1.5 0) (layer "F.Fab") (uuid %s) (effects (font (size 1 1) (thickness 0.15))))' % (q(p.value), q(uid())))
        if p.desc:
            o.append('    (property "Description" %s (at 0 0 0) (layer "F.Fab") (hide yes) (uuid %s) (effects (font (size 1 1) (thickness 0.15))))' % (q(p.desc), q(uid())))
        o.append('    (attr through_hole)')
        for (x1, y1, x2, y2) in p.silk:
            o.append('    (fp_line (start %.4f %.4f) (end %.4f %.4f) (stroke (width 0.15) (type solid)) (layer "F.SilkS") (uuid %s))'
                     % (x1 - p.x, y1 - p.y, x2 - p.x, y2 - p.y, q(uid())))
        for (t, x, y, s) in p.texts:
            o.append('    (fp_text user %s (at %.4f %.4f 0) (layer "F.SilkS") (uuid %s) (effects (font (size %.2f %.2f) (thickness %.3f))))'
                     % (q(t), x - p.x, y - p.y, q(uid()), s, s, max(0.1, s * 0.15)))
        for pd in p.pads:
            o.append('    (pad %s thru_hole %s (at %.4f %.4f) (size %.3f %.3f) (drill %.3f) (layers "*.Cu" "*.Mask") %s (uuid %s))'
                     % (q(pd.num), pd.shape, pd.x - p.x, pd.y - p.y, pd.size, pd.size, pd.drill, net_ref(pd.net), q(uid())))
        for (hx, hy, d) in p.holes:
            o.append('    (pad "" np_thru_hole circle (at %.4f %.4f) (size %.3f %.3f) (drill %.3f) (layers "*.Cu" "*.Mask") (uuid %s))'
                     % (hx - p.x, hy - p.y, d, d, d, q(uid())))
        o.append('  )')
    # контур со скруглёнными углами
    r = 3.0
    o.append('  (gr_line (start %.3f 0) (end %.3f 0) (stroke (width 0.1) (type solid)) (layer "Edge.Cuts") (uuid %s))' % (r, W - r, q(uid())))
    o.append('  (gr_line (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.1) (type solid)) (layer "Edge.Cuts") (uuid %s))' % (W, r, W, H - r, q(uid())))
    o.append('  (gr_line (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.1) (type solid)) (layer "Edge.Cuts") (uuid %s))' % (W - r, H, r, H, q(uid())))
    o.append('  (gr_line (start 0 %.3f) (end 0 %.3f) (stroke (width 0.1) (type solid)) (layer "Edge.Cuts") (uuid %s))' % (H - r, r, q(uid())))
    for (cx, cy, sx, sy) in ((r, r, 0, r), (W - r, r, W - r, 0), (W - r, H - r, W, H - r), (r, H - r, r, H)):
        # дуга: центр, начало, угол 90° против часовой... в KiCad 8 - через start/mid/end
        a0 = math.atan2(sy - cy, sx - cx)
        a1 = a0 + math.pi / 2
        am = (a0 + a1) / 2
        o.append('  (gr_arc (start %.4f %.4f) (mid %.4f %.4f) (end %.4f %.4f) (stroke (width 0.1) (type solid)) (layer "Edge.Cuts") (uuid %s))'
                 % (sx, sy, cx + r * math.cos(am), cy + r * math.sin(am), cx + r * math.cos(a1), cy + r * math.sin(a1), q(uid())))
    for (t, x, y, s) in board_texts:
        o.append('  (gr_text %s (at %.3f %.3f 0) (layer "F.SilkS") (uuid %s) (effects (font (size %.2f %.2f) (thickness %.3f))))'
                 % (q(t), x, y, q(uid()), s, s, max(0.1, s * 0.15)))
    for (x1, y1, x2, y2, L, n) in segs:
        o.append('  (segment (start %.4f %.4f) (end %.4f %.4f) (width %.3f) (layer %s) (net %d) (uuid %s))'
                 % (x1, y1, x2, y2, TRACK, q(LAYERS[L]), n, q(uid())))
    for (x, y, n) in vias:
        o.append('  (via (at %.4f %.4f) (size %.3f) (drill %.3f) (layers "F.Cu" "B.Cu") (net %d) (uuid %s))'
                 % (x, y, VIA_D, VIA_DRILL, n, q(uid())))
    # заливка земли снизу (в KiCad: клавиша B - залить)
    m = 0.6
    o.append('  (zone (net %d) (net_name "GND") (layer "B.Cu") (uuid %s) (hatch edge 0.5)' % (NET_ID["GND"], q(uid())))
    o.append('    (connect_pads (clearance 0.3)) (min_thickness 0.25) (filled_areas_thickness no)')
    o.append('    (fill (thermal_gap 0.5) (thermal_bridge_width 0.5))')
    o.append('    (polygon (pts (xy %.2f %.2f) (xy %.2f %.2f) (xy %.2f %.2f) (xy %.2f %.2f)))' % (m, m, W - m, m, W - m, H - m, m, H - m))
    o.append('  )')
    o.append(')')
    txt = "\n".join(o) + "\n"
    depth = 0
    instr = False
    esc = False
    for ch in txt:                                   # скобки сбалансированы?
        if instr:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                instr = False
        elif ch == '"':
            instr = True
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            assert depth >= 0
    assert depth == 0 and not instr
    open(path, "w", encoding="utf8").write(txt)


def write_pro(path):
    pro = {
        "board": {"design_settings": {"defaults": {"board_outline_line_width": 0.1, "copper_line_width": 0.2},
                                      "rules": {"min_clearance": 0.2, "min_track_width": 0.2, "min_via_diameter": 0.6,
                                                "min_through_hole_diameter": 0.3, "min_hole_to_hole": 0.25}}},
        "meta": {"filename": "deepseek-pocket.kicad_pro", "version": 1},
        "net_settings": {"classes": [{"name": "Default", "clearance": 0.2, "track_width": 0.4, "via_diameter": 0.8,
                                      "via_drill": 0.4, "wire_width": 6, "bus_width": 12, "line_style": 0,
                                      "diff_pair_gap": 0.25, "diff_pair_width": 0.2, "diff_pair_via_gap": 0.25,
                                      "microvia_diameter": 0.3, "microvia_drill": 0.1, "pcb_color": "rgba(0, 0, 0, 0.000)",
                                      "schematic_color": "rgba(0, 0, 0, 0.000)", "priority": 2147483647}],
                         "meta": {"version": 3}},
    }
    open(path, "w", encoding="utf8").write(json.dumps(pro, indent=2, ensure_ascii=False) + "\n")


def preview(segs, vias, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Circle, Rectangle, FancyBboxPatch
    fig, ax = plt.subplots(figsize=(6.4, 10.4), dpi=150)
    ax.add_patch(FancyBboxPatch((0, 0), W, H, boxstyle="round,pad=0,rounding_size=3", fc="#0b5d2a", ec="k"))
    for (x1, y1, x2, y2, L, n) in segs:
        if L == 1:
            ax.plot([x1, x2], [y1, y2], color="#3d7bd9", lw=TRACK * 3.2, solid_capstyle="round", zorder=2)
    for (x1, y1, x2, y2, L, n) in segs:
        if L == 0:
            ax.plot([x1, x2], [y1, y2], color="#e0483e", lw=TRACK * 3.2, solid_capstyle="round", zorder=3)
    for (x, y, n) in vias:
        ax.add_patch(Circle((x, y), VIA_D / 2, fc="#d4b04a", ec="none", zorder=4))
        ax.add_patch(Circle((x, y), VIA_DRILL / 2, fc="k", zorder=5))
    for p in parts:
        for (x1, y1, x2, y2) in p.silk:
            ax.plot([x1, x2], [y1, y2], color="white", lw=0.6, zorder=6)
        for (t, x, y, s) in p.texts:
            ax.text(x, y, t, color="white", fontsize=s * 3.3, ha="center", va="center", zorder=7)
        for pd in p.pads:
            if pd.shape == "rect":
                ax.add_patch(Rectangle((pd.x - pd.size / 2, pd.y - pd.size / 2), pd.size, pd.size, fc="#d4b04a", zorder=6))
            else:
                ax.add_patch(Circle((pd.x, pd.y), pd.size / 2, fc="#d4b04a", zorder=6))
            ax.add_patch(Circle((pd.x, pd.y), pd.drill / 2, fc="k", zorder=7))
        for (hx, hy, d) in p.holes:
            ax.add_patch(Circle((hx, hy), d / 2, fc="white", ec="k", zorder=7))
    for (t, x, y, s) in board_texts:
        ax.text(x, y, t, color="white", fontsize=s * 3.3, ha="center", va="center")
    ax.set_xlim(-1, W + 1)
    ax.set_ylim(H + 1, -1)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_title("DeepSeek pocket v1 - верх (красный) и низ (синий); заливка GND снизу не показана", fontsize=7)
    fig.tight_layout()
    fig.savefig(path)


def main():
    import random
    base = [n for n in nets if n not in ("GND", "+5V", "+3V3")]
    best = None
    for attempt in range(60):
        order = list(base)
        if attempt:
            random.Random(attempt).shuffle(order)
        else:
            order.sort(key=lambda n: sum(1 for (_, pd) in all_pads if pd.net == n))
        order = order + ["+3V3", "+5V", "GND"]
        res, failed = route_all(order)
        if res:
            segs, vias = res
            segs = merge_segments(segs)
            score = len(vias) * 10 + sum(abs(s[2] - s[0]) + abs(s[3] - s[1]) for s in segs)
            if best is None or score < best[0]:
                best = (score, segs, vias, order)
            if attempt >= 3:
                break
        else:
            print("попытка %d: не развелась сеть %s" % (attempt, failed))
    if not best:
        sys.exit("не удалось развести плату")
    _, segs, vias, order = best
    errs = check(segs, vias)
    for e in errs:
        print("ОШИБКА:", e)
    print("сетей %d, отрезков %d, переходов %d, ошибок %d" % (len(nets), len(segs), len(vias), len(errs)))
    write_pcb(segs, vias, os.path.join(HERE, "deepseek-pocket.kicad_pcb"))
    write_pro(os.path.join(HERE, "deepseek-pocket.kicad_pro"))
    preview(segs, vias, os.path.join(HERE, "preview.png"))
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
