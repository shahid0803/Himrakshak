"""Satellite discovery boundary; processing is intentionally outside the API."""

from __future__ import annotations


def discover_sentinel(*args, **kwargs):
    raise RuntimeError("Sentinel discovery requires optional pystac-client and Copernicus authentication")
