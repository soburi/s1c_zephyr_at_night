CAN communication with per-board IDs
====================================

This application assigns separate transmit and receive standard CAN IDs to
each board. When ``sw0`` is pressed, the board sends a one-byte frame using
``CONFIG_BOARD_TX_CAN_ID``. Receiving a frame whose ID matches
``CONFIG_BOARD_RX_CAN_ID`` toggles ``led0`` and prints the received CAN ID.

Connect two boards to the same CAN bus, including CAN transceivers, CANH,
CANL, a common ground, and termination resistors. Build the application for
each ID in the standard 11-bit range ``0x000`` through ``0x7ff``. To make two
boards communicate in both directions, set each board's receive ID to the
other board's transmit ID. For example::

   west build -b <board> can_comm -d build-board-a -- \
     -DCONFIG_BOARD_TX_CAN_ID=0x101 -DCONFIG_BOARD_RX_CAN_ID=0x102
   west build -b <board> can_comm -d build-board-b -- \
     -DCONFIG_BOARD_TX_CAN_ID=0x102 -DCONFIG_BOARD_RX_CAN_ID=0x101

Both IDs default to ``0x028``. They can also be changed with menuconfig under
``Transmit CAN ID`` and ``Receive CAN ID``. Pressing the button on one board
toggles the LED on a board configured to receive its transmit ID.

The selected board must provide the ``zephyr,canbus`` chosen node and the
``sw0`` and ``led0`` aliases. CAN bitrate and pin routing are supplied by the
board's devicetree configuration.
