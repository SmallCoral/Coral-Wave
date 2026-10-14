#!/usr/bin/env python3
"""Synchronize the Coral-Wave PCB with an exported KiCad XML netlist.

This uses KiCad's pcbnew Python API to preserve the current board layout while
updating footprints and pad nets from the schematic.  The extra audio clock
oscillator is placed next to the codec as a starting point for routing.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import pcbnew as pcb


DIRECT_NET_MERGES = {
    "/AVDD_3V3": "+3V3",
    "/MODE": "GND",
}
REFRESH_FROM_LIBRARY = {"CN2"}
NEW_FOOTPRINT_POSITIONS_MM = {"Y2": (130.5, 95.5)}
REPOSITION_MM = {"R73": (134.2, 94.675), "C104": (128.0, 98.5)}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--board", required=True, type=Path)
    parser.add_argument("--netlist", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--stock-footprints", type=Path)
    return parser.parse_args()


def parse_netlist(path: Path) -> tuple[dict, dict, set[str]]:
    root = ET.parse(path).getroot()
    components = {}
    for item in root.findall("./components/comp"):
        ref = item.get("ref", "")
        if not ref or ref.startswith("#"):
            continue
        fields = {
            field.get("name", ""): field.text or ""
            for field in item.findall("./fields/field")
            if field.get("name")
        }
        symbol_pins = {
            pin.get("num", "")
            for pin in item.findall("./units/unit/pins/pin")
            if pin.get("num")
        }
        components[ref] = {
            "value": item.findtext("value", ""),
            "footprint": item.findtext("footprint", ""),
            "uuid": item.findtext("tstamps", ""),
            "fields": fields,
            "symbol_pins": symbol_pins,
        }

    pin_nets = {}
    nets = set()
    for net in root.findall("./nets/net"):
        name = net.get("name", "")
        if not name:
            continue
        # pcbnew escapes '/' inside generated no-connect net names.
        if name.startswith("unconnected-"):
            name = name.replace("/", "{slash}")
        nets.add(name)
        for node in net.findall("node"):
            pin_nets[(node.get("ref", ""), node.get("pin", ""))] = name
    return components, pin_nets, nets


def parse_project_libraries(board_path: Path) -> dict[str, Path]:
    table = board_path.parent / "fp-lib-table"
    libraries = {}
    if not table.exists():
        return libraries
    text = table.read_text(encoding="utf-8")
    for entry in re.findall(r"\(lib\s.*?\(options\s+\"[^\"]*\"\).*?\)", text, re.S):
        name = re.search(r"\(name\s+\"?([^\"\s)]+)\"?\)", entry)
        uri = re.search(r"\(uri\s+\"([^\"]+)\"\)", entry)
        if name and uri:
            resolved = uri.group(1).replace("${KIPRJMOD}", str(board_path.parent))
            libraries[name.group(1)] = Path(os.path.expandvars(resolved))
    return libraries


def footprint_library(
    footprint_id: str, project_libraries: dict[str, Path], stock_root: Path
) -> tuple[str, str, Path]:
    nickname, name = footprint_id.split(":", 1)
    library = project_libraries.get(nickname, stock_root / f"{nickname}.pretty")
    if not library.is_dir():
        raise ValueError(f"Footprint library not found: {nickname} ({library})")
    return nickname, name, library


def ensure_net(board: pcb.BOARD, name: str) -> pcb.NETINFO_ITEM:
    net = board.FindNet(name)
    if net is None:
        board.Add(pcb.NETINFO_ITEM(board, name))
        net = board.FindNet(name)
    if net is None:
        raise RuntimeError(f"Could not create PCB net {name!r}")
    return net


def point_tuple(point: pcb.VECTOR2I) -> tuple[int, int]:
    return point.x, point.y


def copy_text_style(source, destination) -> None:
    destination.SetVisible(source.IsVisible())
    destination.SetPosition(source.GetPosition())
    destination.SetTextSize(source.GetTextSize())
    destination.SetTextThickness(source.GetTextThickness())
    destination.SetTextAngle(source.GetTextAngle())
    destination.SetLayer(source.GetLayer())


def replace_footprint(
    board: pcb.BOARD,
    old: pcb.FOOTPRINT,
    component: dict,
    project_libraries: dict[str, Path],
    stock_root: Path,
) -> pcb.FOOTPRINT:
    nickname, name, library = footprint_library(
        component["footprint"], project_libraries, stock_root
    )
    new = pcb.FootprintLoad(str(library), name)
    if new is None:
        raise ValueError(f"Could not load {component['footprint']} for {old.GetReference()}")

    reference = old.GetReference()
    position = old.GetPosition()
    orientation = old.GetOrientationDegrees()
    layer = old.GetLayer()
    locked = old.IsLocked()
    ref_text = old.Reference()
    value_text = old.Value()

    new.SetReference(reference)
    new.SetValue(component["value"])
    new.SetFPID(pcb.LIB_ID(nickname, name))
    new.SetPath(old.GetPath())
    new.SetPosition(position)
    new.SetOrientationDegrees(orientation)
    if layer != pcb.F_Cu:
        new.SetLayerAndFlip(layer)
    new.SetLocked(locked)
    new.SetExcludedFromBOM(old.IsExcludedFromBOM())
    new.SetExcludedFromPosFiles(old.IsExcludedFromPosFiles())
    new.SetUuid(old.m_Uuid)

    for field_name, field_value in component["fields"].items():
        if field_name in {"Reference", "Value", "Footprint"}:
            continue
        new.SetField(field_name, field_value)
        field = new.GetField(field_name)
        field.SetVisible(False)
        field.SetPosition(position)

    copy_text_style(ref_text, new.Reference())
    copy_text_style(value_text, new.Value())
    board.Remove(old)
    board.Add(new)
    return new


def track_endpoints_for_footprint(footprint: pcb.FOOTPRINT, tracks) -> list[tuple]:
    endpoints = []
    pad_positions = {
        pad.GetNumber(): point_tuple(pad.GetPosition())
        for pad in footprint.Pads()
        if pad.GetNumber()
    }
    for track in tracks:
        start = point_tuple(track.GetStart())
        end = point_tuple(track.GetEnd())
        for number, position in pad_positions.items():
            if start == position:
                endpoints.append((track, True, number))
            if end == position:
                endpoints.append((track, False, number))
    return endpoints


def move_track_endpoints(endpoints: list[tuple], footprint: pcb.FOOTPRINT) -> int:
    new_positions = {
        pad.GetNumber(): pad.GetPosition()
        for pad in footprint.Pads()
        if pad.GetNumber()
    }
    moved = 0
    for track, is_start, pad_number in endpoints:
        position = new_positions.get(pad_number)
        if position is None:
            continue
        if is_start:
            track.SetStart(position)
        else:
            track.SetEnd(position)
        moved += 1
    return moved


def validate_board(board: pcb.BOARD, components: dict, pin_nets: dict) -> dict:
    footprints = {item.GetReference(): item for item in board.GetFootprints()}
    expected_refs = set(components)
    if set(footprints) != expected_refs:
        raise ValueError(
            f"PCB references differ from schematic: "
            f"missing={sorted(expected_refs - set(footprints))}, "
            f"extra={sorted(set(footprints) - expected_refs)}"
        )

    pad_errors = []
    footprint_errors = []
    for ref, component in sorted(components.items()):
        footprint = footprints[ref]
        actual_id = f"{footprint.GetFPID().GetLibNickname()}:{footprint.GetFPID().GetLibItemName()}"
        if actual_id != component["footprint"]:
            footprint_errors.append(
                {"reference": ref, "actual": actual_id, "expected": component["footprint"]}
            )
        actual_numbers = {pad.GetNumber() for pad in footprint.Pads() if pad.GetNumber()}
        missing_numbers = component["symbol_pins"] - actual_numbers
        if missing_numbers:
            pad_errors.append(
                {"reference": ref, "missing_pad_numbers": sorted(missing_numbers)}
            )
        for pad in footprint.Pads():
            number = pad.GetNumber()
            expected_net = pin_nets.get((ref, number))
            if number and expected_net is not None and pad.GetNetname() != expected_net:
                pad_errors.append(
                    {
                        "reference": ref,
                        "pad": number,
                        "actual_net": pad.GetNetname(),
                        "expected_net": expected_net,
                    }
                )
    if footprint_errors or pad_errors:
        raise ValueError(
            f"PCB validation failed: footprints={footprint_errors}, pads={pad_errors}"
        )
    return {
        "footprints": len(footprints),
        "tracks_and_vias": len(list(board.GetTracks())),
        "zones": board.GetAreaCount(),
        "footprint_errors": footprint_errors,
        "pad_errors": pad_errors,
    }


def main() -> None:
    args = parse_args()
    board_path = args.board.resolve()
    components, pin_nets, desired_nets = parse_netlist(args.netlist.resolve())
    board = pcb.LoadBoard(str(board_path))
    project_libraries = parse_project_libraries(board_path)

    original_footprints = {item.GetReference(): item for item in board.GetFootprints()}
    deleted_refs = sorted(set(original_footprints) - set(components))
    added_refs = sorted(set(components) - set(original_footprints))
    if set(added_refs) - set(NEW_FOOTPRINT_POSITIONS_MM):
        raise ValueError(f"Unexpected new PCB footprints require placement: {added_refs}")
    deleted_footprints = {ref: original_footprints[ref] for ref in deleted_refs}

    original_counts = {
        "footprints": len(original_footprints),
        "tracks_and_vias": len(list(board.GetTracks())),
        "zones": board.GetAreaCount(),
    }

    usb = original_footprints.get("USB2")
    usb_endpoints = track_endpoints_for_footprint(usb, list(board.GetTracks())) if usb else []

    for name in sorted(desired_nets):
        ensure_net(board, name)

    stock_root = (
        args.stock_footprints.resolve()
        if args.stock_footprints
        else Path(os.environ["APPDIR"]) / "usr/share/kicad/footprints"
    )

    for ref in added_refs:
        component = components[ref]
        nickname, name, library = footprint_library(
            component["footprint"], project_libraries, stock_root
        )
        footprint = pcb.FootprintLoad(str(library), name)
        if footprint is None:
            raise ValueError(f"Could not load new footprint {component['footprint']}")
        x, y = NEW_FOOTPRINT_POSITIONS_MM[ref]
        footprint.SetPosition(pcb.VECTOR2I(pcb.FromMM(x), pcb.FromMM(y)))
        footprint.SetReference(ref)
        footprint.SetValue(component["value"])
        footprint.SetFPID(pcb.LIB_ID(nickname, name))
        footprint.SetPath(pcb.KIID_PATH("/" + component["uuid"]))
        board.Add(footprint)
        original_footprints[ref] = footprint

    refreshed = []
    for ref in sorted(components):
        old = original_footprints[ref]
        actual_id = f"{old.GetFPID().GetLibNickname()}:{old.GetFPID().GetLibItemName()}"
        if actual_id != components[ref]["footprint"] or ref in REFRESH_FROM_LIBRARY:
            new = replace_footprint(
                board,
                old,
                components[ref],
                project_libraries,
                stock_root,
            )
            original_footprints[ref] = new
            refreshed.append(ref)

    for ref in deleted_refs:
        footprint = deleted_footprints[ref]
        board.Remove(footprint)

    if "Y2" in added_refs:
        for ref, (x, y) in REPOSITION_MM.items():
            footprint = original_footprints.get(ref)
            if footprint is not None:
                footprint.SetPosition(pcb.VECTOR2I(pcb.FromMM(x), pcb.FromMM(y)))

    pad_net_changes = []
    for ref, component in sorted(components.items()):
        footprint = original_footprints[ref]
        footprint.SetValue(component["value"])
        for field_name, field_value in component["fields"].items():
            if field_name not in {"Reference", "Value", "Footprint"}:
                footprint.SetField(field_name, field_value)
                footprint.GetField(field_name).SetVisible(False)
        for pad in footprint.Pads():
            number = pad.GetNumber()
            expected_net = pin_nets.get((ref, number))
            if not number or expected_net is None:
                continue
            old_net = pad.GetNetname()
            if old_net != expected_net:
                pad_net_changes.append(
                    {"reference": ref, "pad": number, "from": old_net, "to": expected_net}
                )
            pad.SetNet(ensure_net(board, expected_net))

    usb_new = original_footprints.get("USB2")
    moved_usb_endpoints = move_track_endpoints(usb_endpoints, usb_new) if usb_new else 0

    for track in board.GetTracks():
        merged = DIRECT_NET_MERGES.get(track.GetNetname())
        if merged:
            track.SetNetCode(ensure_net(board, merged).GetNetCode())

    board.BuildListOfNets()
    board.SanitizeNetcodes()
    board.SynchronizeNetsAndNetClasses(False)
    validation = validate_board(board, components, pin_nets)

    temporary = board_path.with_name(f".{board_path.stem}.sync.tmp.kicad_pcb")
    pcb.SaveBoard(str(temporary), board)
    saved = pcb.LoadBoard(str(temporary))
    saved_validation = validate_board(saved, components, pin_nets)
    os.replace(temporary, board_path)
    temporary.with_suffix(".kicad_pro").unlink(missing_ok=True)

    report = {
        "source_netlist": str(args.netlist.resolve()),
        "board": str(board_path),
        "original": original_counts,
        "result": saved_validation,
        "deleted_references": deleted_refs,
        "refreshed_from_library": refreshed,
        "moved_usb_track_endpoints": moved_usb_endpoints,
        "pad_net_changes": pad_net_changes,
        "validation": validation,
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
