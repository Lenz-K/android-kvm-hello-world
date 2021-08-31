#include <jni.h>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_edu_hm_karbaumer_lenz_android_1kvm_1hello_1world_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}