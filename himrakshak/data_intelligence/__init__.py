"""Real-data intelligence primitives for HimRakshak.

This package is intentionally independent from the demonstration station
runtime. Optional geospatial and ML dependencies are loaded only by the
adapters that need them.
"""

from .registry import PARAMETER_REGISTRY, ParameterDefinition, StudyArea

__all__ = ["PARAMETER_REGISTRY", "ParameterDefinition", "StudyArea"]
