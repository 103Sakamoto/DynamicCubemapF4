# Developer checks

These seven .cpp files are standalone extracted/mock tests, each with its own main() and mock platform objects. They are deliberately separate from Main.cpp and excluded from the xmake plugin target. They do not run in game and do not belong in a player download. Merging them into the plugin would introduce conflicting declarations/entry points.

Compile each separately with a C++20 compiler and run it. They cover DDS loading, encoding, sampled matching, readiness/publication, recovery timing and candidate commit decisions. They are snapshots: changes to the production routines must also update the extracted test code. These checks do not exercise the real D3D11 driver or F4SE runtime.
