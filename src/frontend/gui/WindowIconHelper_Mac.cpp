#include "WindowIconHelper_Mac.h"

#ifndef NO_GUI

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_OSX

#include <SDL.h>

#include <CoreGraphics/CoreGraphics.h>
#include <objc/objc.h>
#include <objc/message.h>
#include <objc/runtime.h>

#include "debug_log.h"

bool WindowIconMac_SetDockIcon(SDL_Surface* surface)
{
    if (!surface) return false;

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    if (!colorSpace) return false;

    CGContextRef ctx = CGBitmapContextCreate(surface->pixels, surface->w, surface->h, 8, surface->pitch,
                                             colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(colorSpace);

    if (!ctx) return false;

    CGImageRef imageRef = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);

    if (!imageRef) return false;

    Class NSImageClass = objc_getClass("NSImage");
    if (!NSImageClass) {
        CGImageRelease(imageRef);
        return false;
    }

    SEL allocSel = sel_registerName("alloc");
    SEL initSel = sel_registerName("initWithCGImage:size:");
    SEL releaseSel = sel_registerName("release");

    id nsImage = ((id(*)(Class, SEL))objc_msgSend)(NSImageClass, allocSel);
    if (!nsImage) {
        CGImageRelease(imageRef);
        return false;
    }

    struct CGSize size = CGSizeMake(static_cast<CGFloat>(surface->w), static_cast<CGFloat>(surface->h));
    nsImage = ((id(*)(id, SEL, CGImageRef, CGSize))objc_msgSend)(nsImage, initSel, imageRef, size);
    CGImageRelease(imageRef);
    if (!nsImage) return false;

    Class NSApplicationClass = objc_getClass("NSApplication");
    if (!NSApplicationClass) {
        ((void(*)(id, SEL))objc_msgSend)(nsImage, releaseSel);
        return false;
    }

    SEL sharedAppSel = sel_registerName("sharedApplication");
    SEL setIconSel = sel_registerName("setApplicationIconImage:");
    id sharedApp = ((id(*)(Class, SEL))objc_msgSend)(NSApplicationClass, sharedAppSel);
    if (!sharedApp) {
        ((void(*)(id, SEL))objc_msgSend)(nsImage, releaseSel);
        return false;
    }

    ((void(*)(id, SEL, id))objc_msgSend)(sharedApp, setIconSel, nsImage);
    ((void(*)(id, SEL))objc_msgSend)(nsImage, releaseSel);

    return true;
}

#endif // defined(__APPLE__) && TARGET_OS_OSX

#endif // NO_GUI


