// SPDX-License-Identifier: MIT
#include "host.h"
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

using namespace std;

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <objc/message.h>
#include <sys/sysctl.h>

static string sysctlText(const char* name) {
    char value[256] = "";
    size_t size = sizeof(value);
    return sysctlbyname(name, value, &size, NULL, 0) == 0 ? value : "unknown";
}

template <class T>
static T sysctlNumber(const char* name) {
    T value = 0;
    size_t size = sizeof(value);
    sysctlbyname(name, &value, &size, NULL, 0);
    return value;
}

HostInfo hostInfo(const string& argv0) {
    HostInfo host;
    host.cpu = sysctlText("machdep.cpu.brand_string");
    host.cpus = sysctlNumber<int>("hw.ncpu");
    host.memoryGiB = sysctlNumber<uint64_t>("hw.memsize")/1073741824.0;
    host.os = sysctlText("kern.osproductversion");
    char exec[PATH_MAX] = "", real[PATH_MAX] = "";
    uint32_t execSize = sizeof(exec);
    host.executable = _NSGetExecutablePath(exec, &execSize) == 0 && realpath(exec, real) != NULL ? real : argv0;
    return host;
}

double memoryMB() {
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t) &info, &count) != KERN_SUCCESS)
        return -1;
    return info.phys_footprint/1048576.0;
}

#ifdef MINICORE_OPENCL
#define CL_SILENCE_DEPRECATION
#include <OpenCL/opencl.h>

// Apple's driver pads some values with a trailing space.
static string deviceText(cl_device_id device, cl_device_info what) {
    size_t size = 0;
    if (clGetDeviceInfo(device, what, 0, NULL, &size) != CL_SUCCESS || size == 0)
        return "unknown";
    vector<char> value(size);
    if (clGetDeviceInfo(device, what, size, value.data(), NULL) != CL_SUCCESS)
        return "unknown";
    string text(value.data());
    text.erase(text.find_last_not_of(' ')+1);
    return text;
}

vector<OpenCLDevice> openclDevices() {
    vector<OpenCLDevice> devices;
    cl_platform_id platform;
    cl_uint count = 0;
    if (clGetPlatformIDs(1, &platform, NULL) != CL_SUCCESS ||
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_CPU, 0, NULL, &count) != CL_SUCCESS)
        return devices;
    vector<cl_device_id> ids(count);
    clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_CPU, count, ids.data(), NULL);
    for (cl_device_id id : ids) {
        OpenCLDevice d;
        d.name = deviceText(id, CL_DEVICE_NAME);
        d.vendor = deviceText(id, CL_DEVICE_VENDOR);
        d.version = deviceText(id, CL_DEVICE_VERSION);
        d.driver = deviceText(id, CL_DRIVER_VERSION);
        d.fp64 = deviceText(id, CL_DEVICE_EXTENSIONS).find("cl_khr_fp64") != string::npos;
        cl_device_type type = 0;
        cl_uint units = 0;
        size_t group = 0;
        clGetDeviceInfo(id, CL_DEVICE_TYPE, sizeof(type), &type, NULL);
        clGetDeviceInfo(id, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(units), &units, NULL);
        clGetDeviceInfo(id, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(group), &group, NULL);
        size_t itemsSize = 0;
        if (clGetDeviceInfo(id, CL_DEVICE_MAX_WORK_ITEM_SIZES, 0, NULL, &itemsSize) == CL_SUCCESS &&
            itemsSize >= sizeof(size_t)) {
            vector<size_t> items(itemsSize/sizeof(size_t));
            if (clGetDeviceInfo(id, CL_DEVICE_MAX_WORK_ITEM_SIZES, itemsSize, items.data(), NULL) == CL_SUCCESS)
                d.maxWorkItem = items[0];
        }
        d.gpu = (type & CL_DEVICE_TYPE_GPU) != 0;
        d.computeUnits = units;
        d.maxWorkGroup = group;
        devices.push_back(d);
    }
    return devices;
}

// On the OpenCL platform, the OpenCL device's own name, vendor, OpenCL version and driver.
GpuInfo gpuInfo(const string& platform, int device) {
    vector<OpenCLDevice> devices = platform == "OpenCL" ? openclDevices() : vector<OpenCLDevice>();
    if (device >= 0 && device < (int) devices.size()) {
        const OpenCLDevice& d = devices[device];
        return {d.name, d.vendor, "none", d.version, d.driver};
    }
#else
GpuInfo gpuInfo(const string&, int) {
#endif
    return {sysctlText("machdep.cpu.brand_string"), "0x106b", "none", "unknown",
            "macOS "+sysctlText("kern.osproductversion")};
}

// Metal's C entry point and two MTLDevice messages, sent as metal-cpp sends them. The device comes back retained.
extern "C" void* MTLCreateSystemDefaultDevice();

string metalDeviceProblem() {
    void* device = MTLCreateSystemDefaultDevice();
    if (device == NULL)
        return "macOS gives this process no Metal device (MTLCreateSystemDefaultDevice() returned nil)";
    const long apple7 = 1007;  // MTLGPUFamilyApple7
    bool usable = ((bool (*)(void*, SEL, long)) objc_msgSend)(device, sel_registerName("supportsFamily:"), apple7);
    // CFStringGetCString writes the terminating zero only when it succeeds.
    CFStringRef nameRef = (CFStringRef) ((void* (*)(void*, SEL)) objc_msgSend)(device, sel_registerName("name"));
    char name[256] = "";
    if (nameRef == NULL || !CFStringGetCString(nameRef, name, sizeof(name), kCFStringEncodingUTF8))
        snprintf(name, sizeof(name), "unnamed");
    CFRelease(device);
    return usable ? "" : "the Metal device "+string(name)+" doesn't support the Apple7 GPU family (M1 or later)";
}

#else
#include <dlfcn.h>
#include <fstream>
#include <sys/utsname.h>

// The value after "key:" on the first line of a /proc file that starts with key, trimmed.
static string procField(const char* path, const string& key) {
    ifstream in(path);
    for (string line; getline(in, line);)
        if (line.compare(0, key.size(), key) == 0 && line.find(':') != string::npos) {
            size_t start = line.find_first_not_of(" \t", line.find(':')+1);
            return start == string::npos ? "" : line.substr(start);
        }
    return "";
}

HostInfo hostInfo(const string& argv0) {
    HostInfo host;
    host.cpu = procField("/proc/cpuinfo", "model name");
    host.cpus = (int) sysconf(_SC_NPROCESSORS_ONLN);
    host.memoryGiB = (double) sysconf(_SC_PHYS_PAGES)*sysconf(_SC_PAGE_SIZE)/1073741824.0;
    utsname name;
    host.os = uname(&name) == 0 ? string(name.sysname)+" "+name.release : "unknown";
    char real[PATH_MAX] = "";
    ssize_t n = readlink("/proc/self/exe", real, sizeof(real)-1);
    host.executable = n > 0 ? string(real, n) : argv0;
    return host;
}

double memoryMB() {
    string rss = procField("/proc/self/status", "VmRSS");  // "123456 kB"
    return rss.empty() ? -1 : atof(rss.c_str())/1024;
}

// The CUDA driver, opened for these lines alone so the core links nothing of NVIDIA's; OpenMM's CUDA plugin
// loads the same library for the Context.
GpuInfo gpuInfo(const string& platform, int device) {
    GpuInfo gpu = {"unknown", "unknown", "unknown", "unknown", "unknown"};
    if (platform != "CUDA")
        return gpu;
    gpu.vendor = "0x10de";
    void* cuda = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
    if (cuda == NULL)
        return gpu;
    auto init = (int (*)(unsigned)) dlsym(cuda, "cuInit");
    auto deviceGet = (int (*)(int*, int)) dlsym(cuda, "cuDeviceGet");
    auto getName = (int (*)(char*, int, int)) dlsym(cuda, "cuDeviceGetName");
    auto getAttribute = (int (*)(int*, int, int)) dlsym(cuda, "cuDeviceGetAttribute");
    auto driverVersion = (int (*)(int*)) dlsym(cuda, "cuDriverGetVersion");
    // CUdevice_attribute values from cuda.h
    enum { PciBusId = 33, PciDeviceId = 34, ComputeMajor = 75, ComputeMinor = 76 };
    int handle, version, bus, slot, major, minor;
    char name[256], pci[16];
    if (init && deviceGet && getName && getAttribute && driverVersion && init(0) == 0 &&
        deviceGet(&handle, device) == 0) {
        if (getName(name, sizeof(name), handle) == 0)
            gpu.device = name;
        if (getAttribute(&bus, PciBusId, handle) == 0 && getAttribute(&slot, PciDeviceId, handle) == 0) {
            snprintf(pci, sizeof(pci), "%02x:%02x:00", bus, slot);
            gpu.pci = pci;
        }
        if (getAttribute(&major, ComputeMajor, handle) == 0 && getAttribute(&minor, ComputeMinor, handle) == 0)
            gpu.compute = to_string(major)+"."+to_string(minor);
        // 1000*major + 10*minor
        if (driverVersion(&version) == 0)
            gpu.driver = to_string(version/1000)+"."+to_string(version%1000/10);
    }
    dlclose(cuda);
    return gpu;
}

string metalDeviceProblem() {
    return "";
}
#endif
