#include <jni.h>
#include <android/log.h>
#include <string>
#include <sys/ioctl.h>
#include <linux/kvm.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/mman.h>
#include <cstdarg>
#include <cerrno>
#include "bare-metal-aarch64/memory.h"

#define MAX_VM_RUNS 20
#define MAX_STRING_LENGTH 100
#define KVM_ARM_VCPU_PSCI_0_2 2

using namespace std;

int kvm, vmfd, vcpufd;
u_int32_t memory_slot_count = 0;
struct kvm_run *run;

int mmio_buffer_index = 0;
char mmio_buffer[MAX_VM_RUNS];

string output_text;
char buffer[MAX_STRING_LENGTH];

/**
 * Execute an ioctl with the given arguments. Exit the program if there is an error.
 *
 * @param file_descriptor
 * @param request
 * @param argument
 * @param name The name of the ioctl request for error output.
 * @return The return value of the ioctl.
 */
int ioctl_exit_on_error(int file_descriptor, unsigned long request, string name, ...) {
    va_list ap;
    va_start(ap, name);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    int ret = ioctl(file_descriptor, request, arg);
    if (ret < 0) {
        snprintf(buffer, MAX_STRING_LENGTH, "System call '%s' failed: %s\n", name.c_str(),
                 strerror(errno));
        output_text += buffer;
        exit(ret);
    }
    return ret;
}

/**
 * Checks the availability of a KVM extension. Exits on errors and if the extension is not available.
 *
 * @param extension The extension identifier to check for.
 * @param name The name of the extension for log statements.
 * @return The return value of the involved ioctl.
 */
int check_vm_extension(int extension, string name) {
    int ret = ioctl(vmfd, KVM_CHECK_EXTENSION, extension);
    if (ret < 0) {
        snprintf(buffer, MAX_STRING_LENGTH,
                 "System call 'KVM_CHECK_EXTENSION' failed: %s\n", strerror(errno));
        output_text += buffer;
        exit(ret);
    }
    if (ret == 0) {
        snprintf(buffer, MAX_STRING_LENGTH, "Extension '%s' not available\n", name.c_str());
        output_text += buffer;
        exit(-1);
    }
    return ret;
}

/**
 * Allocates memory and assigns it to the VM as guest memory.
 *
 * @param memory_len The length of the memory that shall be allocated.
 * @param guest_addr The address of the memory in the guest.
 * @return A pointer to the allocated memory.
 */
uint8_t *allocate_memory_to_vm(size_t memory_len, uint64_t guest_addr, uint32_t flags = 0) {
    void *void_mem = mmap(NULL, memory_len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1,
                          0);
    uint8_t *mem = static_cast<uint8_t *>(void_mem);
    if (!mem) {
        snprintf(buffer, MAX_STRING_LENGTH, "Error while allocating guest memory: %s\n",
                 strerror(errno));
        output_text += buffer;
        exit(-1);
    }

    struct kvm_userspace_memory_region region = {
            .slot = memory_slot_count,
            .flags = flags,
            .guest_phys_addr = guest_addr,
            .memory_size = memory_len,
            .userspace_addr = (uint64_t) mem,
    };
    memory_slot_count++;
    ioctl_exit_on_error(vmfd, KVM_SET_USER_MEMORY_REGION, "KVM_SET_USER_MEMORY_REGION", &region);
    return mem;
}

/**
 * Handles a MMIO exit from KVM_RUN.
 */
void mmio_exit_handler() {
    snprintf(buffer, MAX_STRING_LENGTH, "Is Write: %d\n", run->mmio.is_write);
    output_text += buffer;

    if (run->mmio.is_write) {
        snprintf(buffer, MAX_STRING_LENGTH, "Length: %d\n", run->mmio.len);
        output_text += buffer;
        uint64_t data = 0;
        for (int j = 0; j < run->mmio.len; j++) {
            data |= run->mmio.data[j] << 8 * j;
        }

        mmio_buffer[mmio_buffer_index] = data;
        mmio_buffer_index++;
        snprintf(buffer, MAX_STRING_LENGTH, "Guest wrote 0x%08llX to 0x%08llX\n", data,
                 run->mmio.phys_addr);
        output_text += buffer;
    }
}

/**
 * Logs the reason of a system event exit from KVM_RUN.
 */
void print_system_event_exit_reason() {
    switch (run->system_event.type) {
        case KVM_SYSTEM_EVENT_SHUTDOWN:
            snprintf(buffer, MAX_STRING_LENGTH, "Cause: Shutdown\n");
            output_text += buffer;
            break;
        case KVM_SYSTEM_EVENT_RESET:
            snprintf(buffer, MAX_STRING_LENGTH, "Cause: Reset\n");
            output_text += buffer;
            break;
        case KVM_SYSTEM_EVENT_CRASH:
            snprintf(buffer, MAX_STRING_LENGTH, "Cause: Crash\n");
            output_text += buffer;
            break;
    }
}

/**
 * Closes a file descriptor and therefore frees its resources.
 */
void close_fd(int fd) {
    int ret = close(fd);
    if (ret == -1) {
        snprintf(buffer, MAX_STRING_LENGTH, "Error while closing file: %s\n", strerror(errno));
        output_text += buffer;
    }
}

/**
 * This is a KVM test program for AArch64.
 * As a starting point, this KVM test program for x86 was used: https://lwn.net/Articles/658512/
 * It is explained here: https://lwn.net/Articles/658511/
 * To change the code from x86 to AArch64 the KVM API Documentation (https://www.kernel.org/doc/html/latest/virt/kvm/api.html) and the QEMU source code were used.
 */
int kvm_test() {
    int ret;
    uint8_t *mem;
    size_t mmap_size;

    /* Get the KVM file descriptor */
    kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm < 0) {
        snprintf(buffer, MAX_STRING_LENGTH, "Cannot open '/dev/kvm': %s", strerror(errno));
        output_text += buffer;
        return kvm;
    }

    /* Make sure we have the stable version of the API */
    ret = ioctl(kvm, KVM_GET_API_VERSION, NULL);
    if (ret < 0) {
        snprintf(buffer, MAX_STRING_LENGTH, "System call 'KVM_GET_API_VERSION' failed: %s",
                 strerror(errno));
        output_text += buffer;
        return ret;
    }
    if (ret != 12) {
        snprintf(buffer, MAX_STRING_LENGTH, "expected KVM API Version 12 got: %d", ret);
        output_text += buffer;
        return -1;
    }

    /* Create a VM and receive the VM file descriptor */
    snprintf(buffer, MAX_STRING_LENGTH, "Creating VM\n");
    output_text += buffer;
    vmfd = ioctl_exit_on_error(kvm, KVM_CREATE_VM, "KVM_CREATE_VM", (unsigned long) 0);

    snprintf(buffer, MAX_STRING_LENGTH, "Setting up memory\n");
    output_text += buffer;
    /*
     * MEMORY MAP
     *
     * Start      | Name  | Description
     * -----------+-------+------------
     * 0x10000000 | MMIO  |
     * 0x0401F000 | Stack | grows downwards, so the SP is initially 0x04020000
     * 0x04010000 | Heap  | grows upward
     * 0x04000000 | RAM   |
     * 0x00000000 | ROM   |
     *
     */
    check_vm_extension(KVM_CAP_USER_MEMORY, "KVM_CAP_USER_MEMORY");
    /* ROM Memory */
    mem = allocate_memory_to_vm(0x1000, 0x0);
    // Copy the code into the VM memory
    memcpy(mem, rom_code, sizeof(rom_code));

    /* RAM Memory */
    mem = allocate_memory_to_vm(0x1000, 0x04000000);
    // Copy the code into the VM memory
    memcpy(mem, ram_code, sizeof(ram_code));

    /* Heap Memory */
    allocate_memory_to_vm(0x1000, 0x04010000);
    /* Stack Memory */
    allocate_memory_to_vm(0x1000, 0x0401F000);

    /* MMIO Memory */
    // This will cause a write to 0x10000000, to result in a KVM_EXIT_MMIO.
    check_vm_extension(KVM_CAP_READONLY_MEM, "KVM_CAP_READONLY_MEM");
    allocate_memory_to_vm(0x1000, 0x10000000, KVM_MEM_READONLY);

    /* Create a virtual CPU and receive its file descriptor */
    snprintf(buffer, MAX_STRING_LENGTH, "Creating VCPU\n");
    output_text += buffer;
    vcpufd = ioctl_exit_on_error(vmfd, KVM_CREATE_VCPU, "KVM_CREATE_VCPU", (unsigned long) 0);

    /* Get CPU information for VCPU init */
    snprintf(buffer, MAX_STRING_LENGTH, "Retrieving physical CPU information\n");
    output_text += buffer;
    struct kvm_vcpu_init {
        __u32 target;
        __u32 features[7];
    } preferred_target{};
    ioctl_exit_on_error(vmfd, KVM_ARM_PREFERRED_TARGET, "KVM_ARM_PREFERRED_TARGET",
                        &preferred_target);

    /* Enable the PSCI v0.2 CPU feature, to be able to shut down the VM */
    check_vm_extension(KVM_CAP_ARM_PSCI_0_2, "KVM_CAP_ARM_PSCI_0_2");
    preferred_target.features[0] |= 1 << KVM_ARM_VCPU_PSCI_0_2;

    /* Initialize VCPU */
    snprintf(buffer, MAX_STRING_LENGTH, "Initializing VCPU\n");
    output_text += buffer;
    ioctl_exit_on_error(vcpufd, KVM_ARM_VCPU_INIT, "KVM_ARM_VCPU_INIT", &preferred_target);

    /* Map the shared kvm_run structure and following data. */
    ret = ioctl_exit_on_error(kvm, KVM_GET_VCPU_MMAP_SIZE, "KVM_GET_VCPU_MMAP_SIZE", NULL);
    mmap_size = ret;
    if (mmap_size < sizeof(*run)) {
        snprintf(buffer, MAX_STRING_LENGTH, "KVM_GET_VCPU_MMAP_SIZE unexpectedly small");
        output_text += buffer;
    }
    void *void_mem = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpufd, 0);
    run = static_cast<kvm_run *>(void_mem);
    if (!run) {
        snprintf(buffer, MAX_STRING_LENGTH, "Error while mmap vcpu");
        output_text += buffer;
    }
    /* Repeatedly run code and handle VM exits. */
    snprintf(buffer, MAX_STRING_LENGTH, "Running code\n");
    output_text += buffer;
    bool shut_down = false;
    for (int i = 0; i < MAX_VM_RUNS && !shut_down; i++) {
        snprintf(buffer, MAX_STRING_LENGTH, "\nKVM_RUN Loop %d:\n", i + 1);
        output_text += buffer;
        ret = ioctl(vcpufd, KVM_RUN, NULL);
        if (ret < 0) {
            snprintf(buffer, MAX_STRING_LENGTH, "System call 'KVM_RUN' failed: %d - %s\n",
                     errno, strerror(errno));
            output_text += buffer;
            snprintf(buffer, MAX_STRING_LENGTH,
                     "Error Numbers: EINTR=%d; ENOEXEC=%d; ENOSYS=%d; EPERM=%d\n", EINTR,
                     ENOEXEC, ENOSYS, EPERM);
            output_text += buffer;
            return ret;
        }

        switch (run->exit_reason) {
            case KVM_EXIT_HLT:
                snprintf(buffer, MAX_STRING_LENGTH, "Exit Reason: KVM_EXIT_HLT\n");
                output_text += buffer;
                break;
            case KVM_EXIT_IO:
                snprintf(buffer, MAX_STRING_LENGTH, "Exit Reason: KVM_EXIT_IO\n");
                output_text += buffer;
                break;
            case KVM_EXIT_MMIO:
                snprintf(buffer, MAX_STRING_LENGTH, "Exit Reason: KVM_EXIT_MMIO\n");
                output_text += buffer;
                mmio_exit_handler();
                break;
            case KVM_EXIT_SYSTEM_EVENT:
                // This happens when the VCPU has done a HVC based PSCI call.
                snprintf(buffer, MAX_STRING_LENGTH, "Exit Reason: KVM_EXIT_SYSTEM_EVENT\n");
                output_text += buffer;
                print_system_event_exit_reason();
                shut_down = true;
                break;
            case KVM_EXIT_INTR:
                snprintf(buffer, MAX_STRING_LENGTH, "Exit Reason: KVM_EXIT_INTR\n");
                output_text += buffer;
                break;
            case KVM_EXIT_FAIL_ENTRY:
                snprintf(buffer, MAX_STRING_LENGTH, "Exit Reason: KVM_EXIT_FAIL_ENTRY\n");
                output_text += buffer;
                break;
            case KVM_EXIT_INTERNAL_ERROR:
                snprintf(buffer, MAX_STRING_LENGTH, "Exit Reason: KVM_EXIT_INTERNAL_ERROR\n");
                output_text += buffer;
                break;
            default:
                snprintf(buffer, MAX_STRING_LENGTH, "Exit Reason: other\n");
                output_text += buffer;
        }
    }

    close_fd(vcpufd);
    close_fd(vmfd);
    close_fd(kvm);

    return 0;
}

extern "C" JNIEXPORT jstring JNICALL
Java_edu_hm_karbaumer_lenz_android_1kvm_1hello_1world_MainActivity_kvmHelloWorld(
        JNIEnv *env,
        jobject /* this */) {

    kvm_test();

    string text;
    for (int i = 0; i < mmio_buffer_index; i++) {
        text += mmio_buffer[i];
    }
    return env->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_edu_hm_karbaumer_lenz_android_1kvm_1hello_1world_MainActivity_getKvmHelloWorldLog(
        JNIEnv *env,
        jobject thiz) {
    return env->NewStringUTF(output_text.c_str());
}