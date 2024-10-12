from setuptools import setup, find_packages

setup(
    name="scorbit_sdk",
    version="0.1.0",
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    install_requires=[
        "websockets",
        "requests",
        "ecdsa",
    ],
    extras_require={
        "dev": [
            "pytest",
        ],
    },
    test_suite="tests",
    tests_require=["pytest"],
)