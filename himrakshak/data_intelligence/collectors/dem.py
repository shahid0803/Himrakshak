"""DEM import and terrain derivatives.

ESRI ASCII Grid is supported without third-party packages. GeoTIFF processing
uses Rasterio when installed and never silently falls back to a wrong CRS.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class RasterGrid:
    values: tuple[tuple[float | None, ...], ...]
    xllcorner: float
    yllcorner: float
    cellsize: float
    nodata: float
    crs: str


def _validate_projected_metre_crs(crs: str) -> None:
    if not crs:
        raise ValueError("a projected CRS with metre units is required")
    try:
        from pyproj import CRS  # type: ignore
    except ImportError:
        # Without pyproj, accept only common metre-based projected CRS IDs.
        upper = crs.upper().replace(" ", "")
        if upper not in {"EPSG:32643", "EPSG:32644", "EPSG:3857"}:
            raise ValueError("install pyproj to validate projected CRS units, or use EPSG:32643/EPSG:32644")
        return
    parsed = CRS.from_user_input(crs)
    if not parsed.is_projected:
        raise ValueError("terrain derivatives require a projected CRS")
    axes = parsed.axis_info[:2]
    if len(axes) < 2 or any(axis.unit_conversion_factor is None or abs(axis.unit_conversion_factor - 1.0) > 1e-9 for axis in axes):
        raise ValueError("terrain derivatives require projected CRS axes measured in metres")


def read_ascii_grid(path: str | Path, *, crs: str) -> RasterGrid:
    _validate_projected_metre_crs(crs)
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    header: dict[str, str] = {}
    index = 0
    while index < len(lines) and len(header) < 6:
        key, value = lines[index].split(maxsplit=1)
        header[key.lower()] = value
        index += 1
    required = {"ncols", "nrows", "cellsize"}
    if not required.issubset(header):
        raise ValueError("invalid ESRI ASCII Grid header")
    rows = []
    nodata = float(header.get("nodata_value", "-9999"))
    for line in lines[index:]:
        values = tuple(None if float(value) == nodata else float(value) for value in line.split())
        if values:
            rows.append(values)
    if len(rows) != int(header["nrows"]) or any(len(row) != int(header["ncols"]) for row in rows):
        raise ValueError("ASCII Grid row dimensions do not match header")
    cellsize = float(header["cellsize"])
    x_origin = float(header.get("xllcorner", header.get("xllcenter", 0)))
    y_origin = float(header.get("yllcorner", header.get("yllcenter", 0)))
    # Convert centre coordinates to lower-left corner coordinates once.
    if "xllcorner" not in header and "xllcenter" in header:
        x_origin -= cellsize / 2.0
    if "yllcorner" not in header and "yllcenter" in header:
        y_origin -= cellsize / 2.0
    return RasterGrid(tuple(rows), x_origin, y_origin, cellsize, nodata, crs)


def terrain_features(grid: RasterGrid) -> list[dict[str, Any]]:
    height, width = len(grid.values), len(grid.values[0])
    downstream: dict[tuple[int, int], tuple[int, int] | None] = {}
    for row in range(height):
        for column in range(width):
            elevation = grid.values[row][column]
            if elevation is None:
                continue
            lower: list[tuple[float, int, int]] = []
            for row_delta in (-1, 0, 1):
                for column_delta in (-1, 0, 1):
                    if not row_delta and not column_delta:
                        continue
                    next_row, next_column = row + row_delta, column + column_delta
                    if 0 <= next_row < height and 0 <= next_column < width:
                        next_elevation = grid.values[next_row][next_column]
                        if next_elevation is not None and next_elevation < elevation:
                            lower.append((next_elevation, next_row, next_column))
            downstream[(row, column)] = (min(lower)[1], min(lower)[2]) if lower else None
    flow_accumulation: dict[tuple[int, int], int] = {}

    def flow_count(cell: tuple[int, int], visiting: set[tuple[int, int]] | None = None) -> int:
        if cell in flow_accumulation:
            return flow_accumulation[cell]
        visiting = visiting or set()
        if cell in visiting:
            return 1
        visiting.add(cell)
        total = 1
        for source, target in downstream.items():
            if target == cell:
                total += flow_count(source, visiting)
        flow_accumulation[cell] = total
        return total

    for cell in downstream:
        flow_count(cell)
    output = []
    for row in range(height):
        for column in range(width):
            elevation = grid.values[row][column]
            if elevation is None:
                continue
            def at(r: int, c: int) -> float | None:
                if 0 <= r < height and 0 <= c < width:
                    return grid.values[r][c]
                return None
            west, east = at(row, column - 1), at(row, column + 1)
            north, south = at(row - 1, column), at(row + 1, column)
            if None in (west, east, north, south):
                slope = aspect = curvature = None
            else:
                dzdx = (east - west) / (2 * grid.cellsize)
                dzdy = (south - north) / (2 * grid.cellsize)
                slope = math.degrees(math.atan(math.hypot(dzdx, dzdy)))
                aspect = (math.degrees(math.atan2(dzdy, -dzdx)) + 360) % 360
                curvature = (east + west + north + south - 4 * elevation) / (grid.cellsize ** 2)
            output.append({
                "row": row,
                "column": column,
                # These are projected coordinates, not geographic longitude/latitude.
                "x": grid.xllcorner + (column + 0.5) * grid.cellsize,
                "y": grid.yllcorner + (height - row - 0.5) * grid.cellsize,
                "elevation": elevation,
                "slope_angle": slope,
                "slope_aspect": aspect,
                "terrain_curvature": curvature,
                "flow_accumulation": flow_accumulation[(row, column)],
                "drainage_density": flow_accumulation[(row, column)] / (grid.cellsize ** 2),
                "crs": grid.crs,
                "coordinate_units": "metres",
                "source": "DEM",
                "source_product": "ESRI_ASCII_GRID",
                "spatial_resolution": f"{grid.cellsize} map units",
            })
    return output


def process_dem(path: str | Path, *, crs: str, **kwargs: Any) -> list[dict[str, Any]] | dict[str, Any]:
    suffix = Path(path).suffix.lower()
    if suffix in {".asc", ".ascii"}:
        return terrain_features(read_ascii_grid(path, crs=crs))
    if suffix in {".tif", ".tiff"}:
        try:
            import rasterio  # type: ignore
        except ImportError as exc:
            raise RuntimeError("GeoTIFF processing requires optional rasterio") from exc
        with rasterio.open(path) as dataset:
            if dataset.crs is None:
                raise ValueError("GeoTIFF has no CRS")
            return {
                "path": str(path),
                "crs": str(dataset.crs),
                "width": dataset.width,
                "height": dataset.height,
                "resolution": dataset.res,
                "status": "rasterio_imported_requires_whitebox_derivatives",
            }
    raise ValueError("supported DEM formats are ESRI ASCII Grid and GeoTIFF")
