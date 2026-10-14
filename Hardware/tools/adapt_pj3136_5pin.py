"""Adapt the original LCEDA PJ-3136-B CAD to the project's five-pin footprint.

Run with FreeCAD's bundled Python. Only the extra centre lead below the board
mounting plane is removed; the original STEP remains archived separately.
"""
from pathlib import Path
import hashlib
import json
import re
import FreeCAD as App
import Part

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Hardware/WM8978.3dshapes'
SOURCE = OUT / 'AUDIO-TH_PJ-3136-B_original_6pin.step'
TARGET = OUT / 'AUDIO-TH_PJ-3136-B_5pin.step'
REPORT = ROOT / 'Documentation/Reports/footprint-binding/2026-10-03'
SOURCE_HASH = '4e341e556b1d1906ee3207e52ef7454d5038ad8ea3bcbeb8ae993706c79ff537'
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == SOURCE_HASH

original = Part.Shape()
original.read(str(SOURCE))
assert original.isValid() and len(original.Solids) == 6
solids = list(original.Solids)
# KiCad footprint Y runs opposite to model Y. Pad 3 is at local Y=-3.15;
# keep the positive-Y centre lead and remove the negative-Y centre lead.
extra_pin = Part.makeBox(1.2, 1.2, 3.2, App.Vector(-.1, -3.9, -3.2))
changed = []
doc = App.newDocument('PJ3136FivePin')
objects, colors = [], []
for index, solid in enumerate(solids):
    overlap = solid.common(extra_pin)
    if overlap.Volume > 1e-6:
        changed.append(index)
        solid = solid.cut(extra_pin)
    assert solid.isValid() and len(solid.Solids) == 1
    obj = doc.addObject('Part::Feature', f'Part{index + 1}')
    obj.Shape = solid
    objects.append(obj)
    # Two plastic locators, three plated metal contacts, then the plastic body.
    colors.append((.055, .06, .07) if index in (0, 1, 5) else (.67, .69, .72))
assert changed == [3], 'Unexpected part affected by the cut'
doc.recompute()
Part.export(objects, str(TARGET))

# The headless Part exporter omits display colours; add AP214 solid styles.
text = TARGET.read_text(encoding='utf-8')
solid_ids = re.findall(r'#(\d+) = MANIFOLD_SOLID_BREP\(', text)
contexts = re.findall(r'#(\d+) = \( GEOMETRIC_REPRESENTATION_CONTEXT\(', text)
assert len(solid_ids) == 6 and contexts
entity_id = max(map(int, re.findall(r'#(\d+)\s*=', text)))
lines = []
def add(expression):
    global entity_id
    entity_id += 1
    lines.append(f'#{entity_id} = {expression};')
    return f'#{entity_id}'
styled = []
for solid_id, color in zip(solid_ids, colors):
    rgb = add("COLOUR_RGB(''," + ','.join(str(c) for c in color) + ')')
    fill_color = add(f"FILL_AREA_STYLE_COLOUR('',{rgb})")
    fill = add(f"FILL_AREA_STYLE('',({fill_color}))")
    surface_fill = add(f'SURFACE_STYLE_FILL_AREA({fill})')
    side = add(f"SURFACE_SIDE_STYLE('',({surface_fill}))")
    usage = add(f'SURFACE_STYLE_USAGE(.BOTH.,{side})')
    assignment = add(f'PRESENTATION_STYLE_ASSIGNMENT(({usage}))')
    styled.append(add(f"STYLED_ITEM('',({assignment}),#{solid_id})"))
add("MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('',(" + ','.join(styled) + f'),#{contexts[0]})')
marker = text.rfind('ENDSEC;')
text = text[:marker] + '\n'.join(lines) + '\n' + text[marker:]
text = '\n'.join(line.rstrip() for line in text.splitlines()) + '\n'
TARGET.write_text(text, encoding='utf-8', newline='\n')

restored = Part.Shape()
restored.read(str(TARGET))
assert restored.isValid() and len(restored.Solids) == 6
removed_volume = original.Volume - restored.Volume
assert .5 < removed_volume < 1.0
for obj, solid in zip(objects, restored.Solids):
    assert abs(obj.Shape.Volume - solid.Volume) < 1e-6
clipped_volume = solids[3].common(extra_pin).Volume
# OCC's intersect/cut operations differ slightly for the original curved faces.
# 0.0001 mm^3 is below 0.02% of the removed lead volume.
assert abs(removed_volume - clipped_volume) < 1e-4

# Count independent lead sections 2 mm below the mounting plane.
section = restored.section(Part.makePlane(50, 50, App.Vector(-25, -25, -2), App.Vector(0, 0, 1)))
pin_groups = {}
for vertex in section.Vertexes:
    p = vertex.Point
    centre_x = min([-2.7, .5, 4], key=lambda x: abs(x - p.x))
    side = 1 if p.y > 0 else -1
    pin_groups.setdefault((centre_x, side), []).append([p.x, p.y])
assert set(pin_groups) == {(-2.7, -1), (-2.7, 1), (.5, 1), (4, -1), (4, 1)}
result = {
    'source_file': SOURCE.name, 'source_sha256': SOURCE_HASH,
    'output_file': TARGET.name, 'output_sha256': hashlib.sha256(TARGET.read_bytes()).hexdigest(),
    'valid_step_solid': True, 'solid_count': 6, 'lead_count_at_z_minus_2_mm': 5,
    'removed_extra_lead_model_xy_mm': [.5, -3.15],
    'removed_volume_mm3': removed_volume,
    'cut_box_intersection_volume_mm3': clipped_volume,
    'boolean_volume_tolerance_mm3': 1e-4,
    'all_removed_geometry_within_extra_lead_cut_box': True,
    'no_geometry_added': True, 'pcb_unchanged': True,
    'library_derived_five_pin_adaptation_not_manufacturer_five_pin_cad': True,
}
REPORT.mkdir(parents=True, exist_ok=True)
(REPORT / 'five-pin-adaptation.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
App.closeDocument(doc.Name)
print(json.dumps(result, indent=2))
