# SMASH: Synchronized Many-sided Rowhammer Attacks from JavaScript

Script to trigger (Rowhammer) bit flips on TRR-enabled DDR4 SDRAM through Firefox. Will only work with THP enabled and after having set the target-specific parameters (see comment in source).

The exploitation component (see paper) has been left out.

A demo is available [here](https://www.youtube.com/watch?v=k2D4D-kF-ic). The paper can be found [here](https://www.usenix.org/conference/usenixsecurity21/presentation/ridder).

The `src` directory contains the source code of our native implementation. Please note: highly experimental and poorly documented.
