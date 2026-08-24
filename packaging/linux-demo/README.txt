SimNetLab Demo for x86_64 Linux
================================

Run the demo
------------

1. Extract the complete archive into a writable directory.
2. Open a terminal in the extracted SimNetLab-Demo directory.
3. Run:

       ./run_demo.sh

Close the SimNetLab window to stop both the Client and its local background
Server. Runtime logs are written beneath the local logs directory.

Requirements
------------

- A 64-bit x86 Linux desktop or virtual machine with glibc 2.27 or newer.
- An X11 graphical session, or Wayland with XWayland enabled.
- Working OpenGL graphics support. Software rendering in a virtual machine is
  acceptable, although it may be slower.

The standard X11 libraries supplied by a Linux desktop are used. The C++
runtime and all SimNetLab libraries are included in the executables.

No compiler, package manager, Raylib installation, or SimNetLab source tree is
required.

Troubleshooting
---------------

If the window does not open, run ./run_demo.sh from a terminal and inspect the
message shown there. The corresponding Server console log is written beneath
the logs directory.

The demo uses local UDP port 7777 on loopback. Stop another program using that
port before launching SimNetLab.
