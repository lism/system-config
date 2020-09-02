/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <binder/ProcessState.h>

#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>

#include <ui/DisplayInfo.h>
#include <ui/GraphicTypes.h>
#include <ui/PixelFormat.h>

#include <system/graphics.h>

// TODO: Fix Skia.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <SkImageEncoder.h>
#include <SkData.h>
#include <SkColorSpace.h>
#pragma GCC diagnostic pop

using namespace android;

#define COLORSPACE_UNKNOWN    0
#define COLORSPACE_SRGB       1
#define COLORSPACE_DISPLAY_P3 2

static void usage(const char* pname, PhysicalDisplayId displayId)
{
    fprintf(stderr,
            "usage: %s [-hp] [-d display-id] [FILENAME]\n"
            "   -h: this message\n"
            "   -p: save the file as a png.\n"
            "   -j: save the file as a jpg.\n"
            "   -d: specify the physical display ID to capture (default: %"
                    ANDROID_PHYSICAL_DISPLAY_ID_FORMAT ")\n"
            "       see \"dumpsys SurfaceFlinger --display-id\" for valid display IDs.\n"
            "   -i: specify minimum layer\n"
            "   -a: specify maximum layer\n"
            "   -x: specify the left\n"
            "   -y: specify the top\n"
            "   -X: specify the right\n"
            "   -Y: specify the bottom\n"
            "If FILENAME ends with .png it will be saved as a png.\n"
            "If FILENAME ends with .jpg it will be saved as a jpg.\n"
            "If FILENAME is not given, the results will be printed to stdout.\n",
            pname, displayId);
}

static SkColorType flinger2skia(PixelFormat f)
{
    switch (f) {
        case PIXEL_FORMAT_RGB_565:
            return kRGB_565_SkColorType;
        default:
            return kN32_SkColorType;
    }
}

static sk_sp<SkColorSpace> dataSpaceToColorSpace(ui::Dataspace d)
{
    switch (d) {
        case ui::Dataspace::V0_SRGB:
            return SkColorSpace::MakeSRGB();
        case ui::Dataspace::DISPLAY_P3:
            return SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB, SkNamedGamut::kDCIP3);
        default:
            return nullptr;
    }
}

static uint32_t dataSpaceToInt(ui::Dataspace d)
{
    switch (d) {
        case ui::Dataspace::V0_SRGB:
            return COLORSPACE_SRGB;
        case ui::Dataspace::DISPLAY_P3:
            return COLORSPACE_DISPLAY_P3;
        default:
            return COLORSPACE_UNKNOWN;
    }
}

static status_t notifyMediaScanner(const char* fileName) {
    std::string filePath("file://");
    filePath.append(fileName);
    char *cmd[] = {
        (char*) "am",
        (char*) "broadcast",
        (char*) "am",
        (char*) "android.intent.action.MEDIA_SCANNER_SCAN_FILE",
        (char*) "-d",
        &filePath[0],
        nullptr
    };

    int status;
    int pid = fork();
    if (pid < 0){
       fprintf(stderr, "Unable to fork in order to send intent for media scanner.\n");
       return UNKNOWN_ERROR;
    }
    if (pid == 0){
        int fd = open("/dev/null", O_WRONLY);
        if (fd < 0){
            fprintf(stderr, "Unable to open /dev/null for media scanner stdout redirection.\n");
            exit(1);
        }
        dup2(fd, 1);
        int result = execvp(cmd[0], cmd);
        close(fd);
        exit(result);
    }
    wait(&status);

    if (status < 0) {
        fprintf(stderr, "Unable to broadcast intent for media scanner.\n");
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

template <typename T> void bhj_swap(T& a, T& b)
{
    T t;
    t = a;
    a = b;
    b = t;
}

int main(int argc, char** argv)
{
    std::optional<PhysicalDisplayId> displayId = SurfaceComposerClient::getInternalDisplayId();
    if (!displayId) {
        fprintf(stderr, "Failed to get token for internal display\n");
        return 1;
    }

    const char* pname = argv[0];
    bool png = false;
    bool jpg = false;
    int x = 0;
    int y = 0;
    int X = 0;
    int Y = 0;
    int c;
    uint32_t sticky_status_bar_height = 0;
    while ((c = getopt(argc, argv, "jphd:x:y:X:Y:s:")) != -1) {
        switch (c) {
        case 'p':
            png = true;
            break;
        case 'j':
            jpg = true;
            break;

        case 'd':
            displayId = atoll(optarg);
            break;
        case 'x':
            fprintf(stderr, "x = %s\n", optarg);
            x = atoi(optarg);
            break;
        case 'y':
            y = atoi(optarg);
            break;
        case 'X':
            X = atoi(optarg);
            break;
        case 'Y':
            Y = atoi(optarg);
            break;
        case 's':
            sticky_status_bar_height = atoi(optarg);
            break;
        case '?':
        case 'h':
            usage(pname, *displayId);
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    int fd = -1;
    const char* fn = NULL;
    if (argc == 0) {
        fd = dup(STDOUT_FILENO);
    } else if (argc == 1) {
        fn = argv[0];
        fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0664);
        if (fd == -1) {
            fprintf(stderr, "Error opening file: %s (%s)\n", fn, strerror(errno));
            return 1;
        }
        const int len = strlen(fn);
        if (len >= 4) {
            if (0 == strcmp(fn+len-4, ".png")) {
                png = true;
            } else if (0 == strcmp(fn+len-4, ".jpg")) {
                jpg = true;
            }
        }
    }

    if (fd == -1) {
        usage(pname, *displayId);
        return 1;
    }

    void const* mapbase = MAP_FAILED;
    ssize_t mapsize = -1;

    void* base = NULL;
    uint32_t w, s, h, f;
    size_t size = 0;

    // Maps orientations from DisplayInfo to ISurfaceComposer
    static const uint32_t ORIENTATION_MAP[] = {
        ISurfaceComposer::eRotateNone, // 0 == DISPLAY_ORIENTATION_0
        ISurfaceComposer::eRotate270, // 1 == DISPLAY_ORIENTATION_90
        ISurfaceComposer::eRotate180, // 2 == DISPLAY_ORIENTATION_180
        ISurfaceComposer::eRotate90, // 3 == DISPLAY_ORIENTATION_270
    };

    // setThreadPoolMaxThreadCount(0) actually tells the kernel it's
    // not allowed to spawn any additional threads, but we still spawn
    // a binder thread from userspace when we call startThreadPool().
    // See b/36066697 for rationale
    ProcessState::self()->setThreadPoolMaxThreadCount(0);
    ProcessState::self()->startThreadPool();

    ui::Dataspace outDataspace = ui::Dataspace::V0_SRGB;
    sp<IBinder> display = SurfaceComposerClient::getPhysicalDisplayToken(*displayId);
    if (display == NULL) {
        fprintf(stderr, "Unable to get handle for display %" ANDROID_PHYSICAL_DISPLAY_ID_FORMAT "\n", *displayId);
        // b/36066697: Avoid running static destructors.
        _exit(1);
    }

    Vector<DisplayInfo> configs;
    SurfaceComposerClient::getDisplayConfigs(display, &configs);
    int activeConfig = SurfaceComposerClient::getActiveConfig(display);
    if (static_cast<size_t>(activeConfig) >= configs.size()) {
        fprintf(stderr, "Active config %d not inside configs (size %zu)\n",
                activeConfig, configs.size());
        // b/36066697: Avoid running static destructors.
        _exit(1);
    }
    uint8_t displayOrientation = configs[activeConfig].orientation;
    uint32_t captureOrientation = ORIENTATION_MAP[displayOrientation];

    uint32_t phone_w = configs[activeConfig].w;
    uint32_t phone_h = configs[activeConfig].h;

    uint32_t updateW = X - x;
    uint32_t updateH = Y - y;

    if (displayOrientation % 2 == 1) {
        if (displayOrientation == 1) {
            y = phone_w - y;
            Y = phone_w -Y;
            bhj_swap(y, Y);
        } else {
            x = phone_h - x;
            X = phone_h - X;
            bhj_swap(x, X);
        }
        bhj_swap(x, y);
        bhj_swap(X, Y);

        if (displayOrientation == 1) {
            y += sticky_status_bar_height;
            Y += sticky_status_bar_height;
        }
    }

    sp<GraphicBuffer> outBuffer;
    status_t result = ScreenshotClient::capture(display, ui::Dataspace::V0_SRGB, ui::PixelFormat::RGBA_8888,
                                                Rect(x, y, X, Y), updateW /* reqWidth */,
                                                updateH /* reqHeight */, /* all layers */ false, captureOrientation,
                                                &outBuffer);
    if (result != NO_ERROR) {
        close(fd);
        return 1;
    }

    result = outBuffer->lock(GraphicBuffer::USAGE_SW_READ_OFTEN, &base);

    if (base == nullptr || result != NO_ERROR) {
        String8 reason;
        if (result != NO_ERROR) {
            reason.appendFormat(" Error Code: %d", result);
        } else {
            reason = "Failed to write to buffer";
        }
        fprintf(stderr, "Failed to take screenshot (%s)\n", reason.c_str());
        close(fd);
        return 1;
    }

    w = outBuffer->getWidth();
    h = outBuffer->getHeight();
    s = outBuffer->getStride();
    f = outBuffer->getPixelFormat();
    size = s * h * bytesPerPixel(f);

    fprintf(stderr, "capture ok : x = %u, y = %u, X = %u, Y = %u, w = %u, h = %u, "
            "config_w = %u, config_h = %u, "
            "stride = %u, f = %u, size = %lu, orientation: %u\n",
            x, y, X, Y, w, h,
            configs[activeConfig].w, configs[activeConfig].h,
            s, f, size, displayOrientation);

    if (png || jpg) {
        const SkImageInfo info =
            SkImageInfo::Make(w, h, flinger2skia(f), kPremul_SkAlphaType,
                              dataSpaceToColorSpace(outDataspace));
        SkPixmap pixmap(info, base, s * bytesPerPixel(f));
        struct FDWStream final : public SkWStream {
            size_t fBytesWritten = 0;
            int fFd;
            FDWStream(int f) : fFd(f) {}
            size_t bytesWritten() const override { return fBytesWritten; }
            bool write(const void* buffer, size_t size) override {
                fBytesWritten += size;
                return size == 0 || ::write(fFd, buffer, size) > 0;
            }
        } fdStream(fd);

        SkEncodedImageFormat fmt = png ? SkEncodedImageFormat::kPNG : SkEncodedImageFormat::kJPEG;
        (void)SkEncodeImage(&fdStream, pixmap, fmt, 100);
        if (fn != NULL && !jpg) {
            notifyMediaScanner(fn);
        }
    } else {
        uint32_t c = dataSpaceToInt(outDataspace);
        write(fd, &w, 4);
        write(fd, &h, 4);
        write(fd, &f, 4);
        write(fd, &c, 4);
        size_t Bpp = bytesPerPixel(f);
        for (size_t y=0 ; y<h ; y++) {
            write(fd, base, w*Bpp);
            base = (void *)((char *)base + s*Bpp);
        }
    }
    close(fd);
    if (mapbase != MAP_FAILED) {
        munmap((void *)mapbase, mapsize);
    }

    return 0;
}
