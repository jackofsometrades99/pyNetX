# Configuration file for the Sphinx documentation builder.

import os
import sys

sys.path.insert(0, os.path.abspath("../.."))

project = "pyNetX"
copyright = "2026, Sambhu Nampoothiri G"
author = "Sambhu Nampoothiri G"
release = "2.0.7"
version = "2.0.7"

extensions = []

source_suffix = {
    ".rst": "restructuredtext",
}

templates_path = ["_templates"]
exclude_patterns = ["build", "Thumbs.db", ".DS_Store"]

html_theme = "alabaster"
html_static_path = ["_static"]
html_title = "pyNetX 2.0.7 documentation"
