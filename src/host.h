// SPDX-License-Identifier: MIT
// host: what minicore logs about the machine and its GPU. On macOS it asks sysctl and Mach; on Linux, /proc,
// uname and, for the CUDA platform, the NVIDIA driver.
#ifndef MINICORE_HOST_H
#define MINICORE_HOST_H
#include <string>

struct HostInfo {
    std::string cpu, os, executable;
    int cpus = 0;
    double memoryGiB = 0;
};
// argv0 stands in for the executable's path if the OS can't give it.
HostInfo hostInfo(const std::string& argv0);

// The process's memory in MB: phys_footprint (Activity Monitor's figure) on macOS, VmRSS on Linux; -1 if unknown.
double memoryMB();

// Core 0x27's "GPU info" values for device `device` of OpenMM platform `platform`. An Apple GPU has no PCI
// address or CUDA compute capability, and Metal ships with macOS, so its version stands in for a driver.
// On Linux only CUDA devices are described; any field the driver can't give reads "unknown".
struct GpuInfo {
    std::string device, vendor, pci, compute, driver;
};
GpuInfo gpuInfo(const std::string& platform, int device);
#endif
