#include <jni.h>
#include "core/VarispeedEngine.h"
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>

namespace {
tdspeed::VarispeedEngine* engine(jlong handle) noexcept {
    return reinterpret_cast<tdspeed::VarispeedEngine*>(static_cast<std::uintptr_t>(handle));
}
void fail(JNIEnv* env, const char* type, const char* message) {
    env->ThrowNew(env->FindClass(type), message);
}
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_fyodor_tdspeed_nativebridge_VarispeedEngine_create(
    JNIEnv* env, jobject, jint sampleRate, jint frames, jfloat semitones) {
    try {
        if (frames <= 0 || frames > 1024 * 1024 * 1024 / 8)
            throw std::invalid_argument("Android history exceeds 1 GiB RAM budget");
        auto result = std::make_unique<tdspeed::VarispeedEngine>();
        result->prepare(sampleRate, static_cast<std::size_t>(frames), semitones);
        return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(result.release()));
    } catch (const std::bad_alloc&) {
        fail(env, "java/lang/OutOfMemoryError", "Unable to allocate stereo history");
    } catch (const std::exception& error) {
        fail(env, "java/lang/IllegalArgumentException", error.what());
    }
    return 0;
}

extern "C" JNIEXPORT void JNICALL
Java_com_fyodor_tdspeed_nativebridge_VarispeedEngine_processNative(
    JNIEnv* env, jobject, jlong handle, jobject buffer, jint frames, jfloat semitones, jboolean recenter) {
    auto* pcm = static_cast<float*>(env->GetDirectBufferAddress(buffer));
    const auto bytes = env->GetDirectBufferCapacity(buffer);
    if (!handle || !pcm || reinterpret_cast<std::uintptr_t>(pcm) % alignof(float) != 0
        || frames < 0 || static_cast<jlong>(frames) * 8 > bytes) {
        fail(env, "java/lang/IllegalArgumentException", "Invalid native engine or stereo buffer");
        return;
    }
    // Handle lifetime is serialized by the owning Kotlin object, including close.
    if (!engine(handle)->processInterleaved(pcm, static_cast<std::size_t>(frames), {semitones, recenter == JNI_TRUE}))
        fail(env, "java/lang/IllegalStateException", "Native engine is not prepared");
}

extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_fyodor_tdspeed_nativebridge_VarispeedEngine_snapshotNative(JNIEnv* env, jobject, jlong handle) {
    if (!handle) { fail(env, "java/lang/IllegalStateException", "Engine is closed"); return nullptr; }
    const auto s = engine(handle)->state();
    const jdouble values[] = {static_cast<double>(s.writtenFrames), s.readPosition, s.backlogSeconds,
        s.effectiveRate, static_cast<double>(s.recovery), static_cast<double>(s.recoveryFramesRemaining),
        static_cast<double>(s.recoveryCount), static_cast<double>(s.capacityFrames)};
    auto result = env->NewDoubleArray(8);
    if (result) env->SetDoubleArrayRegion(result, 0, 8, values);
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_fyodor_tdspeed_nativebridge_VarispeedEngine_stateIntoNative(JNIEnv* env, jobject, jlong handle, jobject buffer) {
    auto* values = static_cast<double*>(env->GetDirectBufferAddress(buffer));
    if (!handle || !values || env->GetDirectBufferCapacity(buffer) < 64
        || reinterpret_cast<std::uintptr_t>(values) % alignof(double) != 0) {
        fail(env, "java/lang/IllegalArgumentException", "Invalid native state buffer"); return;
    }
    const auto s = engine(handle)->state();
    values[0] = static_cast<double>(s.writtenFrames); values[1] = s.readPosition;
    values[2] = s.backlogSeconds; values[3] = s.effectiveRate;
    values[4] = static_cast<double>(s.recovery); values[5] = static_cast<double>(s.recoveryFramesRemaining);
    values[6] = static_cast<double>(s.recoveryCount); values[7] = static_cast<double>(s.capacityFrames);
}

extern "C" JNIEXPORT void JNICALL
Java_com_fyodor_tdspeed_nativebridge_VarispeedEngine_destroy(JNIEnv*, jobject, jlong handle) {
    delete engine(handle);
}
