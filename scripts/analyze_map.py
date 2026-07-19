#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Keil armlink .map 文件分析工具（MSPM0G3519 / Arm Compiler 6）

解析 Listings 下生成的 map，输出：
  - 镜像总占用（Flash / RAM）与区域利用率
  - 按 .o 模块统计 Code / RO / RW / ZI
  - 最大函数 / 最大数据符号
  - 被链接器剔除的 unused section 汇总
  - 可选 JSON / CSV 导出

用法:
  python scripts/analyze_map.py
  python scripts/analyze_map.py path/to/xxx.map
  python scripts/analyze_map.py --top 30 --json report.json
  python scripts/analyze_map.py --csv modules.csv --symbols
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from collections import defaultdict
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# 默认路径与芯片容量（与 mspm0g3519.sct / 文档一致）
# ---------------------------------------------------------------------------
REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MAP = (
    REPO_ROOT
    / "project"
    / "mdk"
    / "Listings"
    / "SeekFree_MSPM0G3519_Device_Library.map"
)

# 逻辑容量（scatter 中 LR_IROM1 / RW_IRAM1）
FLASH_MAX = 0x80000  # 512 KB
RAM_MAX = 0x20000  # 128 KB (SRAM partition 0)


# ---------------------------------------------------------------------------
# 数据结构
# ---------------------------------------------------------------------------
@dataclass
class SizeRow:
    name: str
    code: int = 0
    inc_data: int = 0
    ro_data: int = 0
    rw_data: int = 0
    zi_data: int = 0
    debug: int = 0

    @property
    def flash(self) -> int:
        """占用 Flash 的近似值：Code + RO + RW(初值)"""
        return self.code + self.ro_data + self.rw_data

    @property
    def ram(self) -> int:
        """占用 RAM 的近似值：RW + ZI"""
        return self.rw_data + self.zi_data


@dataclass
class Symbol:
    name: str
    value: int
    kind: str  # Thumb Code / Data / Number / Section / ...
    size: int
    object_section: str
    scope: str  # local / global

    @property
    def object_name(self) -> str:
        m = re.search(r"([^/\\]+\.o)\(", self.object_section)
        if m:
            return m.group(1)
        # library form: c_p.l(__main.o) or plain .o
        m = re.search(r"([^/\\]+\.o)$", self.object_section)
        return m.group(1) if m else self.object_section


@dataclass
class ExecRegion:
    name: str
    exec_base: int = 0
    load_base: int = 0
    size: int = 0
    max_size: int = 0
    attrs: str = ""


@dataclass
class LoadRegion:
    name: str
    base: int = 0
    size: int = 0
    max_size: int = 0
    attrs: str = ""
    exec_regions: List[ExecRegion] = field(default_factory=list)


@dataclass
class RemovedSection:
    object_name: str
    section: str
    size: int


@dataclass
class MapReport:
    map_path: str
    component: str = ""
    entry_point: Optional[int] = None
    grand: Dict[str, int] = field(default_factory=dict)
    totals_line: Dict[str, int] = field(default_factory=dict)
    objects: List[SizeRow] = field(default_factory=list)
    libraries: List[SizeRow] = field(default_factory=list)
    library_members: List[SizeRow] = field(default_factory=list)
    load_regions: List[LoadRegion] = field(default_factory=list)
    symbols: List[Symbol] = field(default_factory=list)
    removed: List[RemovedSection] = field(default_factory=list)


# ---------------------------------------------------------------------------
# 解析
# ---------------------------------------------------------------------------
RE_COMPONENT = re.compile(r"^Component:\s*(.+)$")
RE_ENTRY = re.compile(r"Image Entry point\s*:\s*(0x[0-9a-fA-F]+)")
RE_LOAD = re.compile(
    r"Load Region\s+(\S+)\s+\(Base:\s*(0x[0-9a-fA-F]+),\s*Size:\s*(0x[0-9a-fA-F]+),\s*"
    r"Max:\s*(0x[0-9a-fA-F]+)(?:,\s*(.*))?\)"
)
RE_EXEC = re.compile(
    r"Execution Region\s+(\S+)\s+\(Exec base:\s*(0x[0-9a-fA-F]+),\s*"
    r"Load base:\s*(0x[0-9a-fA-F]+),\s*Size:\s*(0x[0-9a-fA-F]+),\s*"
    r"Max:\s*(0x[0-9a-fA-F]+)(?:,\s*(.*))?\)"
)
RE_SIZE_ROW = re.compile(
    r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(.+?)\s*$"
)
RE_SIZE_HEADER = re.compile(
    r"Code\s*\(inc\.\s*data\)\s+RO Data\s+RW Data\s+ZI Data\s+Debug\s+(.+)$"
)
RE_GRAND = re.compile(
    r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(Grand Totals|ELF Image Totals.*|ROM Totals)\s*$"
)
RE_TOTAL_LINE = re.compile(
    r"^\s*Total\s+(RO|RW|ROM)\s+Size[^\d]*(\d+)\s*\(\s*([0-9.]+)\s*kB\)\s*$"
)
RE_REMOVED = re.compile(
    r"^\s*Removing\s+(\S+)\(([^)]+)\),\s*\((\d+)\s*bytes\)\.\s*$"
)
# Symbol: name 可能含空格很少；Value 为 hex 或 " - Undefined Weak Reference"
RE_SYMBOL = re.compile(
    r"^\s{4}(\S(?:.*?\S)?)\s+"
    r"(0x[0-9a-fA-F]+)\s+"
    r"((?:Thumb\s+)?(?:Code|Data)|Number|Section)\s+"
    r"(\d+)\s+"
    r"(.+?)\s*$"
)
RE_SYMBOL_UNDEF = re.compile(
    r"^\s{4}(\S(?:.*?\S)?)\s+-\s+Undefined Weak Reference\s*$"
)


def _hex(s: str) -> int:
    return int(s, 16)


def parse_map(path: Path) -> MapReport:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    report = MapReport(map_path=str(path.resolve()))

    # 分段索引
    idx_removed = _find_line(lines, "Removing Unused input sections from the image")
    idx_sym = _find_line(lines, "Image Symbol Table")
    idx_mem = _find_line(lines, "Memory Map of the image")
    idx_comp = _find_line(lines, "Image component sizes")

    for line in lines[:20]:
        m = RE_COMPONENT.match(line)
        if m:
            report.component = m.group(1).strip()
            break

    if idx_removed is not None:
        end = idx_sym if idx_sym is not None else len(lines)
        report.removed = _parse_removed(lines[idx_removed:end])

    if idx_sym is not None:
        end = idx_mem if idx_mem is not None else len(lines)
        report.symbols = _parse_symbols(lines[idx_sym:end])

    if idx_mem is not None:
        end = idx_comp if idx_comp is not None else len(lines)
        report.load_regions, report.entry_point = _parse_memory_map(lines[idx_mem:end])

    if idx_comp is not None:
        _parse_component_sizes(lines[idx_comp:], report)

    return report


def _find_line(lines: List[str], prefix: str) -> Optional[int]:
    for i, line in enumerate(lines):
        if line.strip().startswith(prefix):
            return i
    return None


def _parse_removed(lines: List[str]) -> List[RemovedSection]:
    out: List[RemovedSection] = []
    for line in lines:
        m = RE_REMOVED.match(line)
        if m:
            out.append(
                RemovedSection(
                    object_name=m.group(1),
                    section=m.group(2),
                    size=int(m.group(3)),
                )
            )
    return out


def _parse_symbols(lines: List[str]) -> List[Symbol]:
    symbols: List[Symbol] = []
    scope = "local"
    for line in lines:
        if line.strip() == "Local Symbols":
            scope = "local"
            continue
        if line.strip() == "Global Symbols":
            scope = "global"
            continue
        if "Undefined Weak Reference" in line:
            continue
        m = RE_SYMBOL.match(line)
        if not m:
            continue
        name = m.group(1).strip()
        if name.startswith("[") or name.endswith(".c") or name.endswith(".s") or name.endswith(".h"):
            # 源文件路径 Number 行、Anonymous 节符号：跳过文件路径 Absolute
            kind = m.group(3)
            if kind == "Number" and m.group(2) == "0x00000000" and int(m.group(4)) == 0:
                continue
        if name == "[Anonymous Symbol]":
            continue
        symbols.append(
            Symbol(
                name=name,
                value=_hex(m.group(2)),
                kind=m.group(3).strip(),
                size=int(m.group(4)),
                object_section=m.group(5).strip(),
                scope=scope,
            )
        )
    return symbols


def _parse_memory_map(lines: List[str]) -> Tuple[List[LoadRegion], Optional[int]]:
    regions: List[LoadRegion] = []
    current: Optional[LoadRegion] = None
    entry: Optional[int] = None

    for line in lines:
        m = RE_ENTRY.search(line)
        if m:
            entry = _hex(m.group(1))
            continue
        m = RE_LOAD.search(line)
        if m:
            current = LoadRegion(
                name=m.group(1),
                base=_hex(m.group(2)),
                size=_hex(m.group(3)),
                max_size=_hex(m.group(4)),
                attrs=(m.group(5) or "").strip(),
            )
            regions.append(current)
            continue
        m = RE_EXEC.search(line)
        if m and current is not None:
            current.exec_regions.append(
                ExecRegion(
                    name=m.group(1),
                    exec_base=_hex(m.group(2)),
                    load_base=_hex(m.group(3)),
                    size=_hex(m.group(4)),
                    max_size=_hex(m.group(5)),
                    attrs=(m.group(6) or "").strip(),
                )
            )
    return regions, entry


def _parse_component_sizes(lines: List[str], report: MapReport) -> None:
    mode: Optional[str] = None  # object | lib_member | library | grand

    for line in lines:
        if "Object Name" in line and "Code" in line:
            mode = "object"
            continue
        if "Library Member Name" in line and "Code" in line:
            mode = "lib_member"
            continue
        if "Library Name" in line and "Code" in line:
            mode = "library"
            continue
        if re.match(r"^={10,}", line.strip()):
            # Grand totals 区：header 后无 name 列
            if mode in ("library", "object", "lib_member", None):
                mode = "grand"
            continue

        m = RE_GRAND.match(line)
        if m:
            label = m.group(7)
            key = "grand" if "Grand" in label else ("elf" if "ELF" in label else "rom")
            report.grand[key] = {
                "code": int(m.group(1)),
                "inc_data": int(m.group(2)),
                "ro_data": int(m.group(3)),
                "rw_data": int(m.group(4)),
                "zi_data": int(m.group(5)),
                "debug": int(m.group(6)),
            }
            continue

        m = RE_TOTAL_LINE.match(line)
        if m:
            report.totals_line[m.group(1).lower()] = int(m.group(2))
            report.totals_line[f"{m.group(1).lower()}_kb"] = float(m.group(3))
            continue

        if mode is None or mode == "grand":
            continue
        if "----" in line or "incl." in line or not line.strip():
            continue
        if line.strip().startswith("Code"):
            continue

        m = RE_SIZE_ROW.match(line)
        if not m:
            continue
        row = SizeRow(
            name=m.group(7).strip(),
            code=int(m.group(1)),
            inc_data=int(m.group(2)),
            ro_data=int(m.group(3)),
            rw_data=int(m.group(4)),
            zi_data=int(m.group(5)),
            debug=int(m.group(6)),
        )
        if row.name.endswith("Totals") or row.name.startswith("("):
            continue
        if mode == "object":
            report.objects.append(row)
        elif mode == "lib_member":
            report.library_members.append(row)
        elif mode == "library":
            report.libraries.append(row)


# ---------------------------------------------------------------------------
# 格式化输出
# ---------------------------------------------------------------------------
def _fmt_bytes(n: int) -> str:
    if n >= 1024 * 1024:
        return f"{n:>8d}  ({n / 1024 / 1024:6.2f} MB)"
    if n >= 1024:
        return f"{n:>8d}  ({n / 1024:6.2f} KB)"
    return f"{n:>8d}  ({n:6d} B )"


def _pct(used: int, total: int) -> str:
    if total <= 0:
        return "  n/a"
    return f"{100.0 * used / total:5.1f}%"


def _bar(used: int, total: int, width: int = 30) -> str:
    if total <= 0:
        return "[" + "?" * width + "]"
    ratio = min(1.0, max(0.0, used / total))
    filled = int(round(ratio * width))
    return "[" + "#" * filled + "-" * (width - filled) + "]"


def print_report(
    report: MapReport,
    top: int = 20,
    show_symbols: bool = True,
    show_removed: bool = True,
) -> None:
    print("=" * 72)
    print("  Keil armlink MAP 分析报告")
    print("=" * 72)
    print(f"  文件: {report.map_path}")
    if report.component:
        print(f"  工具: {report.component}")
    if report.entry_point is not None:
        print(f"  入口: 0x{report.entry_point:08X}")
    print()

    # --- 总占用 ---
    print("-" * 72)
    print("  1. 镜像总占用")
    print("-" * 72)
    g = report.grand.get("grand", {})
    rom_total = report.totals_line.get("rom")
    ro_total = report.totals_line.get("ro")
    rw_total = report.totals_line.get("rw")

    if g:
        code = g.get("code", 0)
        ro = g.get("ro_data", 0)
        rw = g.get("rw_data", 0)
        zi = g.get("zi_data", 0)
        print(f"  Code          {_fmt_bytes(code)}")
        print(f"  RO Data       {_fmt_bytes(ro)}")
        print(f"  RW Data       {_fmt_bytes(rw)}")
        print(f"  ZI Data       {_fmt_bytes(zi)}")
        print()

    flash_used = rom_total if rom_total is not None else (
        (g.get("code", 0) + g.get("ro_data", 0) + g.get("rw_data", 0)) if g else 0
    )
    ram_used = rw_total if rw_total is not None else (
        (g.get("rw_data", 0) + g.get("zi_data", 0)) if g else 0
    )

    print(f"  Flash (ROM)   {_fmt_bytes(flash_used)}  / {_fmt_bytes(FLASH_MAX).strip()}")
    print(f"                {_bar(flash_used, FLASH_MAX)} {_pct(flash_used, FLASH_MAX)}")
    print(f"  RAM  (RW+ZI)  {_fmt_bytes(ram_used)}  / {_fmt_bytes(RAM_MAX).strip()}")
    print(f"                {_bar(ram_used, RAM_MAX)} {_pct(ram_used, RAM_MAX)}")
    if report.totals_line:
        print()
        print(f"  Total RO   Size : {report.totals_line.get('ro', 0)} "
              f"({report.totals_line.get('ro_kb', 0)} kB)")
        print(f"  Total RW   Size : {report.totals_line.get('rw', 0)} "
              f"({report.totals_line.get('rw_kb', 0)} kB)")
        print(f"  Total ROM  Size : {report.totals_line.get('rom', 0)} "
              f"({report.totals_line.get('rom_kb', 0)} kB)")
    print()

    # --- Load / Exec regions ---
    print("-" * 72)
    print("  2. 加载 / 执行区")
    print("-" * 72)
    if not report.load_regions:
        print("  (未解析到 Load Region)")
    for lr in report.load_regions:
        print(
            f"  Load {lr.name:12s}  base=0x{lr.base:08X}  "
            f"size={_fmt_bytes(lr.size).strip()}  "
            f"max={_fmt_bytes(lr.max_size).strip()}  "
            f"use={_pct(lr.size, lr.max_size)}  {_bar(lr.size, lr.max_size, 20)}"
        )
        for er in lr.exec_regions:
            print(
                f"    Exec {er.name:12s} exec=0x{er.exec_base:08X}  "
                f"size={_fmt_bytes(er.size).strip()}  "
                f"max={_fmt_bytes(er.max_size).strip()}  "
                f"use={_pct(er.size, er.max_size)}"
            )
    print()

    # --- Top objects ---
    print("-" * 72)
    print(f"  3. 模块占用 TOP {top}（按 Flash ≈ Code+RO+RW）")
    print("-" * 72)
    objs = sorted(report.objects, key=lambda r: r.flash, reverse=True)
    _print_size_table(objs[:top])
    if report.objects:
        sum_flash = sum(o.flash for o in report.objects)
        sum_ram = sum(o.ram for o in report.objects)
        print(f"  对象合计 Flash≈{_fmt_bytes(sum_flash).strip()}  "
              f"RAM≈{_fmt_bytes(sum_ram).strip()}  "
              f"({len(report.objects)} 个 .o)")
    print()

    print("-" * 72)
    print(f"  4. 模块 RAM 占用 TOP {min(top, 15)}（按 RW+ZI）")
    print("-" * 72)
    objs_ram = sorted(report.objects, key=lambda r: r.ram, reverse=True)
    _print_size_table(objs_ram[: min(top, 15)], sort_hint="RAM")
    print()

    # --- Libraries ---
    if report.libraries:
        print("-" * 72)
        print("  5. 标准库 / 运行时库")
        print("-" * 72)
        _print_size_table(sorted(report.libraries, key=lambda r: r.flash, reverse=True))
        print()

    # --- Largest code / data symbols ---
    if show_symbols and report.symbols:
        code_syms = [
            s
            for s in report.symbols
            if "Code" in s.kind and s.size > 0 and not s.name.startswith("__")
        ]
        data_syms = [
            s
            for s in report.symbols
            if s.kind == "Data" and s.size > 0 and s.name not in ("HEAP", "STACK")
        ]
        # 去重：同名同地址只留一条（local/global 可能重复）
        code_syms = _unique_symbols(code_syms)
        data_syms = _unique_symbols(data_syms)

        print("-" * 72)
        print(f"  6. 最大函数 / Code 符号 TOP {min(top, 25)}")
        print("-" * 72)
        for s in sorted(code_syms, key=lambda x: x.size, reverse=True)[: min(top, 25)]:
            print(
                f"  {s.size:6d} B  0x{s.value:08X}  {s.name[:42]:42s}  {s.object_name}"
            )
        print()

        print("-" * 72)
        print(f"  7. 最大数据符号 TOP {min(top, 25)}（含 BSS/堆栈）")
        print("-" * 72)
        # 额外把 HEAP/STACK 从全量里捞出来展示
        heap_stack = [
            s
            for s in report.symbols
            if s.kind in ("Data", "Section")
            and s.name in ("Heap_Mem", "Stack_Mem", "HEAP", "STACK")
            and s.size > 0
        ]
        shown = _unique_symbols(data_syms + heap_stack)
        for s in sorted(shown, key=lambda x: x.size, reverse=True)[: min(top, 25)]:
            print(
                f"  {s.size:6d} B  0x{s.value:08X}  {s.name[:42]:42s}  {s.object_name}"
            )
        print()

    # --- Removed ---
    if show_removed and report.removed:
        print("-" * 72)
        print("  8. 被剔除的 Unused Section")
        print("-" * 72)
        total_rm = sum(r.size for r in report.removed)
        by_obj: Dict[str, int] = defaultdict(int)
        by_obj_cnt: Dict[str, int] = defaultdict(int)
        for r in report.removed:
            by_obj[r.object_name] += r.size
            by_obj_cnt[r.object_name] += 1
        print(f"  共剔除 {len(report.removed)} 个 section，合计 {_fmt_bytes(total_rm).strip()}")
        print()
        print(f"  按对象汇总 TOP {min(top, 20)}（剔除字节数）:")
        ranked = sorted(by_obj.items(), key=lambda kv: kv[1], reverse=True)
        for name, sz in ranked[: min(top, 20)]:
            print(f"  {sz:8d} B  ({by_obj_cnt[name]:4d} sect)  {name}")
        print()

        # 有实际字节的最大 section
        non_zero = [r for r in report.removed if r.size > 0]
        non_zero.sort(key=lambda r: r.size, reverse=True)
        if non_zero:
            print(f"  最大被剔 section TOP {min(15, top)}:")
            for r in non_zero[: min(15, top)]:
                print(f"  {r.size:6d} B  {r.object_name}({r.section})")
            print()

    # --- 简要建议 ---
    print("-" * 72)
    print("  9. 简要观察")
    print("-" * 72)
    _print_hints(report, flash_used, ram_used)
    print("=" * 72)


def _print_size_table(rows: List[SizeRow], sort_hint: str = "Flash") -> None:
    if not rows:
        print("  (无数据)")
        return
    print(
        f"  {'Code':>7} {'RO':>7} {'RW':>6} {'ZI':>7} "
        f"{'Flash≈':>8} {'RAM≈':>8}  Name"
    )
    print("  " + "-" * 68)
    for r in rows:
        print(
            f"  {r.code:7d} {r.ro_data:7d} {r.rw_data:6d} {r.zi_data:7d} "
            f"{r.flash:8d} {r.ram:8d}  {r.name}"
        )


def _unique_symbols(syms: List[Symbol]) -> List[Symbol]:
    seen = set()
    out = []
    for s in syms:
        key = (s.name, s.value, s.size)
        if key in seen:
            continue
        seen.add(key)
        out.append(s)
    return out


def _print_hints(report: MapReport, flash_used: int, ram_used: int) -> None:
    if not report.objects:
        print("  - 未解析到 Object 尺寸表，请确认 map 完整。")
        return

    top_flash = sorted(report.objects, key=lambda r: r.flash, reverse=True)[:5]
    top_ram = sorted(report.objects, key=lambda r: r.ram, reverse=True)[:5]

    print(f"  - Flash 使用率 {_pct(flash_used, FLASH_MAX).strip()} "
          f"({flash_used}/{FLASH_MAX} B)，RAM {_pct(ram_used, RAM_MAX).strip()} "
          f"({ram_used}/{RAM_MAX} B)。")
    print("  - Flash 最大模块: " + ", ".join(f"{o.name}({o.flash}B)" for o in top_flash))
    print("  - RAM  最大模块: " + ", ".join(f"{o.name}({o.ram}B)" for o in top_ram))

    # Heap / Stack
    for s in report.symbols:
        if s.name == "Heap_Mem" and s.size:
            print(f"  - Heap  : {s.size} B (0x{s.value:08X})")
        if s.name == "Stack_Mem" and s.size:
            print(f"  - Stack : {s.size} B (0x{s.value:08X})")

    lfs = next((o for o in report.objects if o.name == "lfs.o"), None)
    if lfs and lfs.code > 10000:
        print(f"  - LittleFS (lfs.o) Code={lfs.code} B，是 Flash 主要占用之一。")

    param = next((o for o in report.objects if o.name == "param.o"), None)
    if param and param.flash > 5000:
        print(f"  - param.o Flash≈{param.flash} B / RAM≈{param.ram} B，注意字符串与缓冲。")

    startup = next(
        (o for o in report.objects if "startup" in o.name.lower()), None
    )
    if startup and startup.zi_data > 10000:
        print(
            f"  - startup ZI={startup.zi_data} B，通常含 Heap+Stack，"
            "可在 startup 里调 HEAP/STACK 大小。"
        )

    if report.removed:
        total_rm = sum(r.size for r in report.removed)
        print(f"  - 链接器已剔除 unused section 合计 {total_rm} B "
              f"({len(report.removed)} 段)，属正常优化。")


# ---------------------------------------------------------------------------
# 导出
# ---------------------------------------------------------------------------
def export_json(report: MapReport, path: Path) -> None:
    data = {
        "map_path": report.map_path,
        "component": report.component,
        "entry_point": report.entry_point,
        "grand": report.grand,
        "totals_line": report.totals_line,
        "flash_max": FLASH_MAX,
        "ram_max": RAM_MAX,
        "objects": [asdict(o) for o in report.objects],
        "libraries": [asdict(o) for o in report.libraries],
        "load_regions": [
            {
                "name": lr.name,
                "base": lr.base,
                "size": lr.size,
                "max_size": lr.max_size,
                "attrs": lr.attrs,
                "exec_regions": [asdict(er) for er in lr.exec_regions],
            }
            for lr in report.load_regions
        ],
        "removed_summary": {
            "count": len(report.removed),
            "total_bytes": sum(r.size for r in report.removed),
            "by_object": _aggregate_removed(report.removed),
        },
        "top_code_symbols": [
            asdict(s)
            for s in sorted(
                [
                    s
                    for s in report.symbols
                    if "Code" in s.kind and s.size > 0
                ],
                key=lambda x: x.size,
                reverse=True,
            )[:50]
        ],
        "top_data_symbols": [
            asdict(s)
            for s in sorted(
                [s for s in report.symbols if s.kind == "Data" and s.size > 0],
                key=lambda x: x.size,
                reverse=True,
            )[:50]
        ],
    }
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"[OK] JSON 已写入: {path}")


def _aggregate_removed(removed: List[RemovedSection]) -> Dict[str, Dict[str, int]]:
    out: Dict[str, Dict[str, int]] = {}
    for r in removed:
        d = out.setdefault(r.object_name, {"bytes": 0, "sections": 0})
        d["bytes"] += r.size
        d["sections"] += 1
    return out


def export_csv_modules(report: MapReport, path: Path) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(
            ["name", "code", "inc_data", "ro_data", "rw_data", "zi_data",
             "debug", "flash_approx", "ram_approx"]
        )
        for o in sorted(report.objects, key=lambda r: r.flash, reverse=True):
            w.writerow(
                [o.name, o.code, o.inc_data, o.ro_data, o.rw_data, o.zi_data,
                 o.debug, o.flash, o.ram]
            )
    print(f"[OK] 模块 CSV 已写入: {path}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def build_argparser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="分析 Keil armlink .map（Flash/RAM/模块/符号/剔除段）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument(
        "map_file",
        nargs="?",
        default=str(DEFAULT_MAP),
        help=f"map 文件路径（默认: {DEFAULT_MAP.name}）",
    )
    p.add_argument("--top", type=int, default=20, help="TOP N 列表长度（默认 20）")
    p.add_argument("--no-symbols", action="store_true", help="不打印最大符号")
    p.add_argument("--no-removed", action="store_true", help="不打印 unused 剔除统计")
    p.add_argument("--json", metavar="FILE", help="导出 JSON 报告")
    p.add_argument("--csv", metavar="FILE", help="导出模块尺寸 CSV")
    p.add_argument(
        "--flash-max",
        type=lambda x: int(x, 0),
        default=FLASH_MAX,
        help=f"Flash 容量字节（默认 0x{FLASH_MAX:X}）",
    )
    p.add_argument(
        "--ram-max",
        type=lambda x: int(x, 0),
        default=RAM_MAX,
        help=f"RAM 容量字节（默认 0x{RAM_MAX:X}）",
    )
    return p


def main(argv: Optional[List[str]] = None) -> int:
    global FLASH_MAX, RAM_MAX
    args = build_argparser().parse_args(argv)
    FLASH_MAX = args.flash_max
    RAM_MAX = args.ram_max

    map_path = Path(args.map_file)
    if not map_path.is_file():
        print(f"[ERROR] 找不到 map 文件: {map_path}", file=sys.stderr)
        print(
            "  请先在 Keil 中完整编译，或指定正确路径。",
            file=sys.stderr,
        )
        return 1

    report = parse_map(map_path)
    print_report(
        report,
        top=max(1, args.top),
        show_symbols=not args.no_symbols,
        show_removed=not args.no_removed,
    )

    if args.json:
        export_json(report, Path(args.json))
    if args.csv:
        export_csv_modules(report, Path(args.csv))

    return 0


if __name__ == "__main__":
    sys.exit(main())
