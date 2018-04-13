LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_ARCH),arm)
 M5_ASM := gem5/m5op_arm_.S
else ifeq ($(TARGET_ARCH),arm64)
 M5_ASM := gem5/m5op_arm_.S
else
 $(error cannot build Gem5Pipe for $(TARGET_ARCH))
endif

$(call emugl-begin-shared-library,libOpenglSystemCommon)
$(call emugl-import,libGLESv1_enc libGLESv2_enc lib_renderControl_enc)

LOCAL_SRC_FILES := \
    goldfish_dma.cpp \
    FormatConversions.cpp \
    HostConnection.cpp \
    ProcessPipe.cpp    \
    QemuPipeStream.cpp \
    ThreadInfo.cpp \
    Gem5PipeStream.cpp \
    $(M5_ASM)

ifdef IS_AT_LEAST_OPD1
LOCAL_HEADER_LIBRARIES += libnativebase_headers

$(call emugl-export,HEADER_LIBRARIES,libnativebase_headers)
endif

ifdef IS_AT_LEAST_OPD1
LOCAL_HEADER_LIBRARIES += libhardware_headers
$(call emugl-export,HEADER_LIBRARIES,libhardware_headers)
endif

$(call emugl-export,C_INCLUDES,$(LOCAL_PATH) bionic/libc/private)

$(call emugl-end-module)
