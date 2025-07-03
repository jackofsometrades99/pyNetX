### setup.py
from pathlib import Path
from skbuild import setup

# Read the contents of your README file
this_directory = Path(__file__).parent
long_description = (this_directory / "README.md").read_text(encoding="utf-8")

setup(
    name="pyNetX",
    version="1.0.9",
    description="NETCONF client with truly async capabilities",
    long_description=long_description,
    long_description_content_type="text/markdown",
    author="Sambhu Nampoothiri G",
    license="Apache-2.0",
    packages=["pyNetX"],
    cmake_args=[
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DPYBIND11_DETAILED_ERROR_MESSAGES=ON",
    ],
    zip_safe=False,
)
