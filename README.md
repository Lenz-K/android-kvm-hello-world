# Prerequisites

This app will only run successful on Android 12.
Also the permissions of the file `/dev/kvm` on Android need to be changed,
so that all users have read and write permission.
See the last section of this file for more information.

# About

This app runs a [c++ program](https://github.com/Lenz-K/android-kvm-hello-world/blob/main/app/src/main/cpp/kvm_test.cpp), 
that sets up a KVM AArch64 VM and runs a bare metal AArch64 hello-world-program in the VM.
The bare metal program is included as an ELF file [hello_world.elf](https://github.com/Lenz-K/android-kvm-hello-world/tree/main/app/src/main/assets/bin).
It is developed in another [repository](https://github.com/Lenz-K/arm64-kvm-hello-world/tree/main/bare-metal-aarch64).
The app was tested on a Cortex-A72 (ARMv8-A) processor running Android 12.

![Screenshot](Screenshot.png)

# Changing the permissions of '/dev/kvm'

Changing the permissions of `/dev/kvm` requires root privileges on the Android device.
First connect adb to the device.
Instruction for different setups can be found [here](https://developer.android.com/studio/command-line/adb).

To change the permissions until the next boot, issue the following command:
```
adb shell chmod a+rw /dev/kvm
```

To change the permissions on every boot, one can add the command to an init file of an Android device.
There are usually several of these files and name and location can vary.
Find more information on this [here](https://android.googlesource.com/platform/system/core/+/master/init/README.md).
On the development device of this repo, the file was located at `/etc/init/init-debug.rc`.
To edit the file, pull it to your machine:
```
adb pull /etc/init/init-debug.rc
```

Than add the following lines to the end of the file.
```
on boot
    chmod a+rw /dev/kvm
```

Then push it back to the device.
```
adb push init-debug.rc /etc/init/init-debug.rc
```
On the next boot, the permissions should be changed automatically.
