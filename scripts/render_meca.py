"""Clean ISO renders of the housing + cover STLs via PyVista / VTK.

Run after re-exporting the STLs from FreeCAD :

    python3 scripts/render_meca.py meca

The PNGs land in meca/renders/ and are referenced from meca/README.md.

Dependencies (one-shot install) :

    python3 -m pip install pyvista
"""
import sys
from pathlib import Path

import pyvista as pv

pv.set_plot_theme("dark")


# Material colors. Project accent palette : ocean blue housing, lighter
# cover so it reads as a separate piece even at a glance.
HOUSING_COLOR = "#1d4d6e"   # primary accent
COVER_COLOR   = "#3aa6ff"   # accent / hover variant


def render_one(stl_path: Path, out_path: Path, title: str, color: str = HOUSING_COLOR):
    mesh = pv.read(str(stl_path))
    p = pv.Plotter(off_screen=True, window_size=(1600, 1200))
    p.background_color = "#f0f0f4"
    p.add_mesh(
        mesh,
        color=color,
        smooth_shading=False,  # show the printed segments
        ambient=0.35,
        diffuse=0.65,
        specular=0.20,
        specular_power=18,
        metallic=0.05,
        roughness=0.55,
    )
    p.enable_lightkit()
    p.view_isometric()
    p.camera.zoom(1.15)
    p.add_text(title, position="upper_edge", color="#222222", font_size=14)
    p.screenshot(str(out_path), transparent_background=False)
    p.close()
    print(f"wrote {out_path}")


def render_assembled(housing_path: Path, cover_path: Path, out_path: Path, title: str,
                     cover_lift_mm: float = 8.0):
    """Lift the cover by `cover_lift_mm` above the housing top so the
    viewer reads them as two parts ; otherwise the flush join hides the
    seam and the assembled view looks like a single block."""
    housing = pv.read(str(housing_path))
    cover = pv.read(str(cover_path))
    h_zmax = housing.bounds[5]
    c_zmin = cover.bounds[4]
    cover.translate((0, 0, h_zmax - c_zmin + cover_lift_mm), inplace=True)

    p = pv.Plotter(off_screen=True, window_size=(1600, 1200))
    p.background_color = "#f0f0f4"
    p.add_mesh(housing, color=HOUSING_COLOR, smooth_shading=False,
               ambient=0.35, diffuse=0.65, specular=0.20, specular_power=18,
               roughness=0.55)
    p.add_mesh(cover, color=COVER_COLOR, smooth_shading=False,
               ambient=0.35, diffuse=0.65, specular=0.25, specular_power=20,
               roughness=0.5)
    p.enable_lightkit()
    p.view_isometric()
    p.camera.zoom(1.10)
    p.add_text(title, position="upper_edge", color="#222222", font_size=14)
    p.screenshot(str(out_path), transparent_background=False)
    p.close()
    print(f"wrote {out_path}")


if __name__ == "__main__":
    meca = Path(sys.argv[1]).resolve()
    out_dir = meca / "renders"
    out_dir.mkdir(exist_ok=True)
    render_one(
        meca / "somfyrts2mqtt_housing-box_housing.stl",
        out_dir / "housing-iso.png",
        "Housing",
        color=HOUSING_COLOR,
    )
    render_one(
        meca / "somfyrts2mqtt_housing-box_cover.stl",
        out_dir / "cover-iso.png",
        "Cover",
        color=COVER_COLOR,
    )
    render_assembled(
        meca / "somfyrts2mqtt_housing-box_housing.stl",
        meca / "somfyrts2mqtt_housing-box_cover.stl",
        out_dir / "assembled-iso.png",
        "Assembled enclosure",
    )
