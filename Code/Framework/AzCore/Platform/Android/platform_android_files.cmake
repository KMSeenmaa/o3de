#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

set(FILES
    AzCore/PlatformId/PlatformId_Platform.h
    AzCore/PlatformId/PlatformId_Android.h
    ../Common/UnixLike/AzCore/std/parallel/internal/condition_variable_UnixLike.h
    ../Common/UnixLike/AzCore/std/parallel/internal/mutex_UnixLike.h
    ../Common/UnixLike/AzCore/std/parallel/internal/semaphore_UnixLike.h
    ../Common/UnixLike/AzCore/std/parallel/internal/thread_UnixLike.cpp
    ../Common/UnixLike/AzCore/std/parallel/internal/thread_UnixLike.h
    ../Common/UnixLike/AzCore/std/parallel/internal/time_UnixLike.h
    AzCore/std/parallel/internal/condition_variable_Platform.h
    AzCore/std/parallel/internal/mutex_Platform.h
    AzCore/std/parallel/internal/semaphore_Platform.h
    AzCore/std/parallel/internal/thread_Android.cpp
    AzCore/std/parallel/internal/thread_Platform.h
    AzCore/std/parallel/config_Android.h
    AzCore/std/parallel/config_Platform.h
    AzCore/std/string/fixed_string_Platform.inl
    ../Common/Clang/AzCore/std/string/fixed_string_Clang.inl
    ../Common/Unimplemented/AzCore/PlatformIncl_Unimplemented.h
    ../Common/UnixLike/AzCore/Platform_UnixLike.cpp
    AzCore/PlatformIncl_Platform.h
    AzCore/Serialization/Locale_Platform.h
    AzCore/Settings/CommandLineParser_Platform.h
    ../Common/UnixLike/AzCore/Serialization/Locale_UnixLike.h
    ../Common/UnixLike/AzCore/Serialization/Locale_UnixLike.cpp
    ../Common/UnixLike/AzCore/Process/ProcessInfo_UnixLike.cpp
    ../Common/Unimplemented/AzCore/Debug/StackTracer_Unimplemented.cpp
    ../Common/UnixLike/AzCore/Debug/Trace_UnixLike.cpp
    AzCore/Debug/Trace_Android.cpp
    ../Common/Default/AzCore/IO/Streamer/StreamerConfiguration_Default.cpp
    ../Common/Default/AzCore/IO/Streamer/StreamerContext_Default.cpp
    ../Common/Default/AzCore/IO/Streamer/StreamerContext_Default.h
    ../Common/UnixLike/AzCore/IO/AnsiTerminalUtils_UnixLike.cpp
    ../Common/UnixLike/AzCore/IO/FileIO_UnixLike.cpp
    ../Common/UnixLike/AzCore/IO/SystemFile_UnixLike.cpp
    ../Common/UnixLike/AzCore/IO/Internal/SystemFileUtils_UnixLike.h
    ../Common/UnixLike/AzCore/IO/Internal/SystemFileUtils_UnixLike.cpp
    AzCore/IO/Streamer/StreamerContext_Platform.h
    AzCore/IO/SystemFile_Android.cpp
    AzCore/IO/SystemFile_Android.h
    AzCore/IO/SystemFile_Platform.h
    AzCore/IPC/SharedMemory_Platform.h
    ../Common/UnixLike/AzCore/Memory/OSAllocator_UnixLike.h
    AzCore/Memory/OSAllocator_Platform.h
    ../Common/Default/AzCore/Module/Internal/ModuleManagerSearchPathTool_Default.cpp
    AzCore/Math/Internal/MathTypes_Android.h
    AzCore/Math/Random_Platform.h
    ../Common/UnixLike/AzCore/Math/Random_UnixLike.cpp
    ../Common/UnixLike/AzCore/Math/Random_UnixLike.h
    ../Common/UnixLike/AzCore/Module/DynamicModuleHandle_UnixLike.cpp
    AzCore/Module/DynamicModuleHandle_Android.cpp
    AzCore/NativeUI/NativeUISystemComponent_Android.cpp
    ../Common/UnixLike/AzCore/Settings/CommandLineParser_UnixLike.cpp
    ../Common/UnixLike/AzCore/Settings/CommandLineParser_UnixLike.h
    ../Common/UnixLike/AzCore/Socket/AzSocket_fwd_UnixLike.h
    ../Common/UnixLike/AzCore/Socket/AzSocket_UnixLike.cpp
    ../Common/UnixLike/AzCore/Socket/AzSocket_UnixLike.h
    AzCore/Socket/AzSocket_fwd_Platform.h
    AzCore/Socket/AzSocket_Platform.h
    ../Common/UnixLike/AzCore/std/time_UnixLike.cpp
    AzCore/Utils/Utils_Android.cpp
    AzCore/Android/AndroidEnv.cpp
    AzCore/Android/AndroidEnv.h
    AzCore/Android/APKFileHandler.cpp
    AzCore/Android/APKFileHandler.h
    AzCore/Android/ApiLevel.h
    AzCore/Android/Utils.cpp
    AzCore/Android/Utils.h
    AzCore/Android/JNI/JNI.cpp
    AzCore/Android/JNI/JNI.h
    AzCore/Android/JNI/Object.h
    AzCore/Android/JNI/Object_fwd.h
    AzCore/Android/JNI/scoped_ref.h
    AzCore/Android/JNI/shared_ref.h
    AzCore/Android/JNI/Signature.h
    AzCore/Android/JNI/Internal/ClassName.h
    AzCore/Android/JNI/Internal/JStringUtils.h
    AzCore/Android/JNI/Internal/JStringUtils_impl.h
    AzCore/Android/JNI/Internal/Object_impl.h
    AzCore/Android/JNI/Internal/Signature_impl.h
    AzCore/Debug/Profiler_Platform.inl
    AzCore/Debug/Profiler_Android.inl
)
if (LY_TEST_PROJECT)
    ly_add_source_properties(
        SOURCES ${CMAKE_CURRENT_LIST_DIR}/AzCore/Android/Utils.cpp
        PROPERTY COMPILE_DEFINITIONS
        VALUES LY_NO_ASSETS
    )
endif()

# Disabling -ffast-math for 'IntersectSegment.cpp' file only.
# It has an implementation of the "Watertight Ray/Triangle Intersection"
# algorithm, which gets broken by the fast-math optimizations.
ly_add_source_properties(
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/../../AzCore/Math/IntersectSegment.cpp
    PROPERTY COMPILE_OPTIONS
    VALUES -fno-fast-math -Wno-overriding-t-option
)
