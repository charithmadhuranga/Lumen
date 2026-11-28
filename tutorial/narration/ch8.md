# ch8

The Hardware page is where Lumen meets the board.

The Board tab lists every profile, built-in and custom, and shows which one is active. Built-in profiles are read-only. To change pins, you duplicate a profile and edit the copy.

Profiles can be exported and imported as JSON files, and each one is validated before it is applied.

Applying a profile is transactional. If a driver fails to start with the new profile, the controller rolls back to the profile that worked.

Items that have not been confirmed on real hardware are marked as unverified in the profile, so you always know what has been tested.

The Pin mapping tab shows every GPIO on the chip. You can see which pins are assigned, which are reserved for flash, which are input-only, and which are strapping pins that affect how the chip boots. Conflicts are highlighted.

The Peripherals tab shows each driver and bus with its live health. On the KC868-AP, that is the I2C bus, the PCA9685 PWM chip, the two relays, the two input expanders and the direct inputs.

From here you can scan the I2C bus and see which addresses answer. That is the first thing to check when a board does not behave as expected.

The Outputs tab lists every output and the light that uses it. You can drive an output directly with a test level, and on the PCA9685, the value can be read back from the chip's own registers.

The Inputs tab shows every input live, and the Diagnostics tab shows the health of the controller over time: free memory, the longest lighting tick, hardware errors, dropped commands and connected clients.
