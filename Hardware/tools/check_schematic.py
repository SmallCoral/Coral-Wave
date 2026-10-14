#!/usr/bin/env python3
"""Check the current SAMD21 + WM8978 circuit from a KiCad XML netlist.

Run KiCad's native ERC as well: these checks cover intended signal topology and
clock-domain separation, which ERC cannot infer from pin electrical types.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SCH = ROOT / "Hardware/Coral-Wave.kicad_sch"
DEFAULT_NETLIST = ROOT / "Documentation/Reports/schematic-review/2026-09-23-dual-clock/netlist.xml"


def load(path: Path):
    tree = ET.parse(path)
    components = {c.attrib["ref"]: c for c in tree.findall("./components/comp")}
    nets = {}
    pin_net = {}
    for net in tree.findall("./nets/net"):
        name = net.attrib["name"]
        members = {f'{n.attrib["ref"]}.{n.attrib["pin"]}' for n in net.findall("node")}
        nets[name] = members
        for member in members:
            if member in pin_net:
                raise AssertionError(f"Duplicate pin: {member}")
            pin_net[member] = name
    return components, nets, pin_net


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--netlist", type=Path, default=DEFAULT_NETLIST)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    components, nets, pin_net = load(args.netlist)
    checks = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            raise AssertionError(message)
        checks.append(message)

    def exact(net: str, pins: str) -> None:
        expected = set(pins.split())
        require(nets.get(net) == expected, f"{net}: exactly {sorted(expected)}")

    def contains(net: str, pins: str) -> None:
        expected = set(pins.split())
        require(expected <= nets.get(net, set()), f"{net}: contains {sorted(expected)}")

    require(len(components) == 54, "54 fitted schematic components")
    removed = {f"TP{i}" for i in range(1, 13)} | {"R62", "R72", "R77", "R79", "R80", "SW1", "U18"}
    require(not (removed & set(components)), "diagnostic and redundant parts removed")
    for ref, frequency, mpn in [
        ("Y1", "12MHz", "ASE-12.000MHZ-LC-T"),
        ("Y2", "12.288MHz", "ASE-12.288MHZ-L-C-T"),
    ]:
        comp = components[ref]
        require(frequency in comp.findtext("value", ""), f"{ref} frequency {frequency}")
        require(
            comp.findtext("./fields/field[@name='MPN']", "") == mpn,
            f"{ref} part number {mpn}",
        )
    exact("/MCU_12M", "Y1.3 U1.15")
    exact("/AUDIO_OSC", "Y2.3 R73.1")
    exact("/AUDIO_MCLK", "R73.2 U16.11")
    exact("/AUDIO_CLK_EN", "U1.19 Y1.1 Y2.1 R74.1")
    contains("+3V3", "Y1.4 Y2.4 C103.1 C104.1 U1.9 U1.30 U16.31")
    contains("GND", "Y1.2 Y2.2 C103.2 C104.2 R74.2 U1.10 U1.28 U16.18")
    require(pin_net["Y1.3"] != pin_net["Y2.3"], "MCU and codec clocks independent")

    exact("/I2S_DACDAT", "U1.8 U16.10")
    exact("/I2S_ADCDAT", "U1.11 U16.9")
    exact("/CODEC_BCLK", "U16.8 R75.1")
    exact("/I2S_BCLK", "R75.2 U1.13")
    exact("/CODEC_LRCLK", "U16.7 R76.1")
    exact("/I2S_LRCLK", "R76.2 U1.14")
    contains("/CODEC_SDA", "U1.21 U16.17 R63.1")
    contains("/CODEC_SCL", "U1.22 U16.16 R64.1")
    exact("/USB_DP", "R53.2 U1.24")
    exact("/USB_DM", "R54.2 U1.23")
    exact("/SWCLK", "J2.3 R71.2 U1.31")
    exact("/SWDIO", "J2.2 U1.32")
    contains("/VBUS_RAW", "USB2.A4 USB2.A9 USB2.B4 USB2.B9 U17.1 U17.3 C105.1")
    contains("/VBUS_5V", "U17.6 U15.3 U16.26 C75.1 C77.1 C92.2 C93.2")
    contains("+3V3", "U15.2 U15.4 U16.13 U16.14 C106.1 C107.1")
    exact("/VDDCORE", "U1.29 C101.1")
    exact("Net-(U16-ROUT1)", "U16.29 C86.1")
    exact("Net-(C86-Pad2)", "C86.2 CN2.3")
    exact("Net-(U16-LOUT1)", "U16.30 C87.1")
    exact("Net-(C87-Pad2)", "C87.2 CN2.4")
    exact("Net-(U16-ROUT2)", "U16.23 SPK2.1")
    exact("Net-(U16-LOUT2)", "U16.25 SPK2.2")

    result = {
        "verdict": "pass",
        "checks_passed": len(checks),
        "component_count": len(components),
        "source_sha256": hashlib.sha256(SCH.read_bytes()).hexdigest(),
        "checks": checks,
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(checks)} topology checks; {len(components)} components")


if __name__ == "__main__":
    main()
