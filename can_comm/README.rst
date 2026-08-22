Simple CAN peer communication
=============================

This application sends a standard CAN frame with ID ``0x123`` whenever
``sw0`` is pressed. Receiving a frame with the same ID toggles ``led0``.

Connect two boards to the same CAN bus, including CAN transceivers, CANH,
CANL, a common ground, and termination resistors. Both boards can run the
same firmware; pressing the button on one board toggles the LED on the other.

The selected board must provide the ``zephyr,canbus`` chosen node and the
``sw0`` and ``led0`` aliases. CAN bitrate and pin routing are supplied by the
board's devicetree configuration.
