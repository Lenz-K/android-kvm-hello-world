# Prerequisites

This App will only run successful on Android 12. Also the permissions of the file `/dev/kvm` on android need to be changed, so that all users have read and write permission.

# About

This App runs a [c++ program](https://github.com/Lenz-K/android-kvm-hello-world/blob/main/app/src/main/cpp/kvm_test.cpp), 
that sets up a KVM ARM64-VM and runs a bare metal ARM64 Hello-World-Program in the VM.
The bare metal program is included in binary form in the file [memory.h](https://github.com/Lenz-K/android-kvm-hello-world/blob/main/app/src/main/cpp/bare-metal-arm64/memory.h).
It is developed in another [repository](https://github.com/Lenz-K/arm64-kvm-hello-world/tree/main/bare-metal-arm64).
