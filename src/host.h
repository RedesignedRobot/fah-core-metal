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

// Why OpenMM's Metal platform can't use the device MTLCreateSystemDefaultDevice() gives, as it does for its
// Context: nil, or a GPU outside the Apple7 family (M1 and later), such as a VM's paravirtual one; "" if usable.
std::string metalDeviceProblem();

#ifdef MINICORE_OPENCL
#include <vector>
// The GPU and CPU devices of the first OpenCL platform, in the order OpenMM's OpenCL platform numbers them
// for DeviceIndex (OpenCLContext asks for CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_CPU). macOS has one platform.
struct OpenCLDevice {
    std::string name, vendor, version, driver;
    bool gpu = false, fp64 = false;
    int computeUnits = 0;
    long maxWorkGroup = 0, maxWorkItem = 0;  // CL_DEVICE_MAX_WORK_GROUP_SIZE, CL_DEVICE_MAX_WORK_ITEM_SIZES[0]
};
std::vector<OpenCLDevice> openclDevices();
#endif
#endif
