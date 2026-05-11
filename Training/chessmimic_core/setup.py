"""
Setup script for the chessmimic_core package.
"""

from setuptools import setup, find_packages

setup(
    name="chessmimic_core",
    version="0.1.0",
    packages=find_packages(),
    package_data={
        "chessmimic_core": ["py.typed", "*.pyi"],
    },
    install_requires=[
        "numpy>=1.20.0",
    ],
    python_requires=">=3.8",
    description="C++ accelerated functions for ChessMimic",
    author="ChessMimic Team",
    author_email="noreply@example.com",
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
    ],
)