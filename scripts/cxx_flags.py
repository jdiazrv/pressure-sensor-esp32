Import("env")

# Silence legacy C++ keyword warnings from third-party libraries without
# affecting the C compiler flags used by the ESP32 framework sources.
env.Append(CXXFLAGS=["-Wno-register"])
