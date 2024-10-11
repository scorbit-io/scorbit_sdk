#Scorbit Python SDK

Scorbit API (REST &amp; Websocket) implementation for Python 3

This SDK package is designed for python environments, including arcade games and virtual games, to integrate with the Scorbit platform.

Installation:

To install as a package into site-packages:

python setup.py install

or

To install as a package in its current place but create symlinks to it within site-packages:

python setup.py develop

Usage:

Please refer to the examples/basic_example.py for a quick start implementation of the lower level features. Once you successfully are able to provision and authenticate using the provided parameters, you can then begin to use the higher level game logic calls to push your game state to the Scorbit API.

Credits:

This code is heavily inspired by both the work of Koen Heltzel at Dutch Pinball as well as elements fromthe "VPIN Python Test Example" by @daphishbowl/