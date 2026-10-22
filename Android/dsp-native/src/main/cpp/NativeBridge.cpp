#include <jni.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

extern "C" JNIEXPORT jstring JNICALL
Java_com_fyodor_tdspeed_nativebridge_NativeBridge_identity(JNIEnv* env, jobject) {
#if defined(__aarch64__)
    return env->NewStringUTF("tdspeed-p0/1 arm64-v8a stereo-float32");
#elif defined(__x86_64__)
    return env->NewStringUTF("tdspeed-p0/1 x86_64 stereo-float32");
#else
    return env->NewStringUTF("tdspeed-p0/1 unsupported-abi");
#endif
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_fyodor_tdspeed_nativebridge_NativeBridge_process(
    JNIEnv* env, jobject, jobject buffer, jint count, jfloat gain, jboolean swap) {
    auto* samples = static_cast<float*>(env->GetDirectBufferAddress(buffer));
    const auto capacity = env->GetDirectBufferCapacity(buffer);
    if (!samples || count < 0 || count % 2 || capacity < static_cast<jlong>(count) * 4
        || !std::isfinite(gain) || gain < 0.f || gain > 1.f) {
        env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Invalid stereo diagnostic buffer or gain");
        return 0;
    }
    float inputPeak = 0.f, outputPeak = 0.f;
    for (int i = 0; i < count; i += 2) {
        const float left = std::isfinite(samples[i]) ? samples[i] : 0.f;
        const float right = std::isfinite(samples[i+1]) ? samples[i+1] : 0.f;
        inputPeak = std::max(inputPeak, std::max(std::abs(left), std::abs(right)));
        samples[i] = (swap ? right : left) * gain;
        samples[i+1] = (swap ? left : right) * gain;
        outputPeak = std::max(outputPeak, std::max(std::abs(samples[i]), std::abs(samples[i+1])));
    }
    std::uint32_t a = 0, b = 0;
    std::memcpy(&a, &inputPeak, sizeof(a)); std::memcpy(&b, &outputPeak, sizeof(b));
    return static_cast<jlong>((static_cast<std::uint64_t>(a) << 32) | b);
}
