#import <sys/sysctl.h>
#import <netinet/tcp.h>
#import <sys/socket.h>
#import <arpa/inet.h>
#import <netdb.h>
#import <netdb.h>
#import <zlib.h>
#import <WebKit/WebKit.h>

#import "RepeatoHeaders.h"
#import "Logger.h"
#import "InfoMessages.h"

#ifndef REPEATO_PORT
//#define INJECTION_PORT 31442
//#define APPCODE_PORT 31444
//#define XPROBE_PORT 31448
#define REPEATO_PORT 31449
#endif

#ifndef REPEATO_APPNAME
#define REPEATO_APPNAME RepeatoCapture
#endif
#define REPEATO_MAGIC -141414141
//#define REPEATO_MINDIFF (4*sizeof(rmencoded_t))
//#define REPEATO_COMPRESSED_OFFSET 1000000000

// Various wire formats used.
//#define REPEATO_NOKEY 3 // Original format
#define REPEATO_VERSION 4 // Sends source file path for security check
//#define MINICAP_VERSION 1 // https://github.com/openstf/minicap#usage
#define HYBRID_VERSION 2 // minicap but starting with "Remote" header

// May be used for security
#define REPEATO_KEY @__FILE__
#define REPEATO_XOR 0xc5

// Times coordinate-resolution to capture.
#ifndef REPEATO_OVERSAMPLE
#ifndef REPEATO_HYBRID
#define REPEATO_OVERSAMPLE 1.0
#else
#define REPEATO_OVERSAMPLE *(float *)device.remote.scale
#endif
#endif

#ifndef REPEATO_JPEGQUALITY
#define REPEATO_JPEGQUALITY 0.5
#endif

#ifndef REPEATO_RETRIES
#define REPEATO_RETRIES 3
#endif

#ifdef REPEATO_HYBRID
// Wait for screen to settle before capture
#ifndef REPEATO_DEFER
#define REPEATO_DEFER 0.5
#endif

// Only wait this long for screen to settle
#ifndef REPEATO_MAXDEFER
#define REPEATO_MAXDEFER 0.1
#endif
#else
// Wait for screen to settle before capture
#ifndef REPEATO_DEFER
#define REPEATO_DEFER 0.5
#endif

// Only wait this long for screen to settle
#ifndef REPEATO_MAXDEFER
#define REPEATO_MAXDEFER 0.1
#endif
#endif

#ifdef DEBUG
#define RMLog NSLog
#else
#define RMLog while(0) NSLog
#endif

#ifdef REPEATO_DEBUG
#define RMDebug NSLog
#else
#define RMDebug while(0) NSLog
#endif

#define REPEATO_NOW [NSDate timeIntervalSinceReferenceDate]
#ifdef REPEATO_BENCHMARK
#define RMBench printf
#else
#define RMBench while(0) printf
#endif

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <os/log.h>

#ifdef REPEATO_LEGACY
static BOOL repeatoLegacy = TRUE;
#else
static BOOL repeatoLegacy = FALSE;
#endif

typedef unsigned rmpixel_t;
typedef unsigned rmencoded_t;

/// Shaows UITouchPhase enum but available to Appkit code in server.
typedef NS_ENUM(int, RMTouchPhase) {
    RMTouchBeganDouble = -1,
    RMTouchBegan = 0,
    RMTouchMoved,
    RMTouchStationary,
    RMTouchEnded,
    RMTouchCancelled,
    RMTouchUseScale,
    RMTouchRegionEntered,
    RMTouchRegionMoved,
    RMTouchRegionExited,
    RMTouchInsertText = 100
};

/// struct sent from client when it connects to rendering server
/// Can be either original Remote format or "minicap".
struct _rmdevice {
    char version;
    union {
        struct {
            char machine[24];
            char appname[64];
            char appvers[24];
            char hostname[63];
            char scale[4]; // float
            char isIPad[4]; // int
            char protocolVersion[4];// int
            char displaySize[12]; // 9999x9999
            char deviceName[24];
            char systemVersion[4]; //float
            char appFrameWorkType[4]; //int 0: default, 1: flutter
            char expansion[16];
            char magic[4]; // int
        } remote;
    };
};

/// struct send before each iage frame sen to render server
struct _rmframe {
    NSTimeInterval timestamp;
    union {
        /// image paramters
        struct { float width, height, imageScale; };
        /// If length < 0, retails of a recorded touch
        struct { float x, y; RMTouchPhase phase; };
    };
    /// length of image data or if < 0 -touch number
    int length;
};

///// If image is compressed, it's uncompressed length is sent
//struct _rmcompress {
//    unsigned bytes; unsigned char data[1];
//};

#define RMMAX_TOUCHES 2

/// Struct sent from UI of server to replay touches in the client
struct _rmevent {
    NSTimeInterval timestamp;
    RMTouchPhase phase;
    union {
        struct { float x, y; } touches[RMMAX_TOUCHES];
    };
    /* int padded; */
};

@interface REPEATO_APPNAME: NSObject {
@package
    rmpixel_t *buffer, *buffend;
    CGContextRef cg;
}
- (instancetype)initFrame:(const struct _rmframe *)frame;
- (NSData *)subtractAndEncode:(REPEATO_APPNAME *)prevbuff;
- (CGImageRef)cgImage;
@end

@protocol RepeatoDelegate <NSObject>
@required
- (void)remoteConnected:(BOOL)status;
@end

#if defined(REPEATO_IMPL) || \
    defined(__IPHONE_OS_VERSION_MIN_REQUIRED) && defined(DEBUG)

#ifndef __IPHONE_OS_VERSION_MIN_REQUIRED
#import <Cocoa/Cocoa.h>
#else
#import <UIKit/UIKit.h>
#import <objc/runtime.h>

@interface REPEATO_APPNAME(Client)
+ (void)startCapture:(NSString *)addrs scaleUpFactor:(float)s;
+ (void)shutdown;
@end

static NSTimeInterval timestamp0;
static UITouch *currentTouch;
static NSSet *currentTouches;
static BOOL lateJoiners;

@interface RCFakeEvent: UIEvent {
@public
    NSTimeInterval _timestamp;
}

@end

@implementation RCFakeEvent

- (instancetype)init {
    if ((self = [super init])) {
        _timestamp = REPEATO_NOW - timestamp0;
    }
    return self;
}

- (NSTimeInterval)timestamp {
    return _timestamp;
}

- (UITouch *)_firstTouchForView:(UIView *)view {
    RMDebug(@"_firstTouchForView: %@", view);
    return currentTouch;
}

- (NSSet *)touchesForView:(UIView *)view {
    RMDebug(@"touchesForWindow:%@", view);
    return currentTouches;
}

- (NSSet *)touchesForWindow:(UIWindow *)window {
    RMDebug(@"touchesForWindow:%@", window);
    return currentTouches;
}

- (NSSet *)touchesForGestureRecognizer:(UIGestureRecognizer *)rec {
    RMDebug(@"touchesForGestureRecognizer:%@", rec);
    return currentTouches;
}

- (void)_removeTouch:(UITouch *)touch fromGestureRecognizer:(UIGestureRecognizer *)rec {
    RMDebug(@"_removeTouch:%@ fromGestureRecognizer:%@", touch, rec);
}

- (NSSet *)allTouches {
    return currentTouches;
}

- (void)_addWindowAwaitingLatentSystemGestureNotification:(id)a0 deliveredToEventWindow:(id)a1 {
    RMDebug(@"_addWindowAwaitingLatentSystemGestureNotification:%@ deliveredToEventWindow:%@", a0, a1);
}

- (NSUInteger)_buttonMask {
    return 0;
}

- (UIEventType)type {
    return (UIEventType)0;
}

@end

@interface UIView(Description)
- (NSString *)recursiveDescription;
@end

@interface NSObject(ForwardReference)

- (void *)_copyRenderLayer:(void *)a0 layerFlags:(unsigned)a1 commitFlags:(unsigned *)a2;
- (void *)in_copyRenderLayer:(void *)a0 layerFlags:(unsigned)a1 commitFlags:(unsigned *)a2;

- (void)_didCommitLayer:(void *)a0;
- (void)in_didCommitLayer:(void *)a0;

@end

@interface UIApplication(ForwardReference)
- (void)in_sendEvent:(UIEvent *)event;
@end
@implementation UITouch(Identifier)
- (void)_setTouchIdentifier:(unsigned int)ident {
    Ivar ivar = class_getInstanceVariable([self class], "_touchIdentifier");
    ptrdiff_t offset = ivar_getOffset(ivar);
    unsigned *iptr = (unsigned *)((char *)(__bridge void *)self + offset);
    *iptr = ident;
}
@end
#endif

@interface REPEATO_APPNAME()
@property (strong, nonatomic) CADisplayLink *displayLink;
@end

/// The class defined by RepeatoCapture is actually a buffer
/// used to work with the memory representation of screenshots
@implementation REPEATO_APPNAME

- (instancetype)initFrame:(const struct _rmframe *)frame {
    if ((self = [super init])) {
        CGSize size = {frame->width*frame->imageScale, frame->height*frame->imageScale};
        int bitsPerComponent = 8, bytesPerRow = bitsPerComponent/8*4 * (int)size.width;
        int bufferSize = bytesPerRow * (int)size.height;
        cg = CGBitmapContextCreate(NULL, size.width, size.height, bitsPerComponent,
                                   bytesPerRow, CGColorSpaceCreateDeviceRGB(),
                                   (CGBitmapInfo)kCGImageAlphaNoneSkipFirst);
        buffer = (rmpixel_t *)CGBitmapContextGetData(cg);
        buffend = (rmpixel_t *)((char *)buffer + bufferSize);
        if (repeatoLegacy) {
            CGContextTranslateCTM(cg, 0, size.height);
            CGContextScaleCTM(cg, frame->imageScale, -frame->imageScale);
        }
        else
            CGContextScaleCTM(cg, frame->imageScale, frame->imageScale);
    }
    return self;
}

/// Made-up image encoding format
/// @param prevbuff previous image
- (NSData *)subtractAndEncode:(REPEATO_APPNAME *)prevbuff {
    unsigned tmpsize = 64*1024;
    rmencoded_t *tmp = (rmencoded_t *)malloc(tmpsize * sizeof *tmp), *end = tmp + tmpsize;

    rmencoded_t *out = tmp, count = 0, expectedDiff = 0, check = 0;
    *out++ = prevbuff == nil;

    for (const rmpixel_t *curr = buffer, *prev = prevbuff ? prevbuff->buffer : NULL;
             curr < buffend; check += *curr, curr++) {
        rmpixel_t ref = (prev ? *prev++ : 0), diff = *curr - ref - expectedDiff;
        if (!diff && curr != buffer)
            count++;
        else {
            if (count) {
                if (count < 0xff)
                    out[-1] |= count;
                else {
                    out[-1] |= 0xff;
                    *out++ = count;
                }
                count = 0;
            }

            *out++ = diff & 0xffffff00;

            if (out + 4 >= end) {
                size_t ptr = out - tmp;
                tmpsize *= 1.5;
                tmp = (rmencoded_t *)realloc(tmp, tmpsize * sizeof *tmp);
                out = tmp + ptr;
                end = tmp + tmpsize;
            }
        }

        expectedDiff = *curr - ref;
    }

    if (count) {
        if (count < 0xff)
            out[-1] |= count;
        else {
            out[-1] |= 0xff;
            *out++ = count;
        }
    }

    *out++ = check;

    return [NSData dataWithBytesNoCopy:tmp length:(char *)out - (char *)tmp freeWhenDone:YES];
}

/// Convert buffer into an image.
- (CGImageRef)cgImage {
    return CGBitmapContextCreateImage(cg);
}

- (void)dealloc {
    CGContextRelease(cg);
}

#ifdef __IPHONE_OS_VERSION_MIN_REQUIRED

static id<RepeatoDelegate> repeatoDelegate;
static NSMutableArray<NSValue *> *connections;
static char *connectionKey;


/// Initiate screen capture and processing of events from RemoteUI server
/// @param addrs space separated list of IPV4 addresses or hostnames
+ (void)startCapture:(NSString *)addrs scaleUpFactor:(float)s {
    scaleUpFactor = s == 0 ? 1 : s;
    [UIApplication.sharedApplication setIdleTimerDisabled:true];
    Log(self, @"Start capture at '%@' with scaleUpFactor %.2f...", addrs, scaleUpFactor);
    [self performSelectorInBackground:@selector(backgroundConnect:)
                           withObject:addrs];
}

/// Connect in the backgrand rather than hold application up.
/// @param addrs space separate list of IPV4 addresses or hostnames
+ (BOOL)backgroundConnect:(NSString *)addrs {
    
    NSMutableArray *newConnections = [NSMutableArray new];
    for (NSString *addr in [addrs componentsSeparatedByString:@" "]) {
        NSArray<NSString *> *parts = [addr componentsSeparatedByString:@":"];
        NSString *inaddr = parts[0];
        in_port_t port = REPEATO_PORT;
        if (parts.count > 1)
            port = (in_port_t)parts[1].intValue;
            Log(self, @"Connecting to %@:%d...", inaddr, port);
        int remoteSocket = [self connectIPV4:inaddr.UTF8String port:port];
        if (remoteSocket) {
            isConnectedWithHost = true;
            [InfoMessages.shared onConnect];
            Log(self, @"Connected to %@:%d.", inaddr, port);
            FILE *writeFp = fdopen(remoteSocket, "w");
            [newConnections addObject:[NSValue valueWithPointer:writeFp]];
        }
    }
    if (!newConnections.count)
        return FALSE;

    static dispatch_once_t once;
    dispatch_once(&once, ^{
        [self initCapture];
    });

    int32_t keylen = (int)strlen(connectionKey);
    for (NSValue *fp in newConnections) {
        FILE *writeFp = (FILE *)fp.pointerValue;
        int headerSize = 1 + sizeof device.remote;
        if (fwrite(&device, 1, headerSize, writeFp) != headerSize)
            Log(self, @"Could not write device info: %s", strerror(errno));
        else if (device.version == REPEATO_VERSION &&
                 fwrite(&keylen, 1, sizeof keylen, writeFp) != sizeof keylen)
            Log(self, @"Could not write keylen: %s", strerror(errno));
        else if (device.version == REPEATO_VERSION &&
                 fwrite(connectionKey, 1, keylen, writeFp) != keylen)
            Log(self, @"Could not write key: %s", strerror(errno));
        else
            [self performSelectorInBackground:@selector(processEvents:) withObject:fp];
    }

    dispatch_async(writeQueue, ^{
        [connections addObjectsFromArray:newConnections];
        [self queueCapture];
        lateJoiners = TRUE;
    });
    [repeatoDelegate remoteConnected:TRUE];
    return TRUE;
}

/// Parse addres and attempt to connect to a "RemoteUI" server
/// @param ipAddress IPV4 address or hostname
/// @param port well known port for remote server
+ (int)connectIPV4:(const char *)ipAddress port:(in_port_t)port {
    static struct sockaddr_in remoteAddr;

    remoteAddr.sin_len = sizeof remoteAddr;
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(port);

    if (isdigit(ipAddress[0])){
        int valid = inet_aton(ipAddress, &remoteAddr.sin_addr);
        Log(self, @"Adress valid (0 == invalid): %d", valid);
    } else {
        struct hostent *ent = gethostbyname2(ipAddress, remoteAddr.sin_family);
        if (ent)
            memcpy(&remoteAddr.sin_addr, ent->h_addr_list[0], sizeof remoteAddr.sin_addr);
        else {
            Log(self, @"Could not look up host '%s'", ipAddress);
            return 0;
        }
    }

    Log(self, @"Attempting connection to: %s:%d", ipAddress, port);
    return [self connectAddr:(struct sockaddr *)&remoteAddr];
}

/// Try to connect to specified internet address
/// @param remoteAddr parse/looked-up address
+ (int)connectAddr:(struct sockaddr *)remoteAddr {
    int remoteSocket, optval = 1;
    if ((remoteSocket = socket(remoteAddr->sa_family, SOCK_STREAM, 0)) < 0)
        Log(self, @"Could not open socket for injection: %s", strerror(errno));
    else if (setsockopt(remoteSocket, IPPROTO_TCP, TCP_NODELAY, (void *)&optval, sizeof(optval)) < 0)
        Log(self, @"Could not set TCP_NODELAY: %s", strerror(errno));
    else
        for (int retry = 0; retry<REPEATO_RETRIES; retry++) {
            if (retry)
                [NSThread sleepForTimeInterval:1.0];
            Log(self,@"Try #%d", retry);
            if (connect(remoteSocket, remoteAddr, remoteAddr->sa_len) >= 0){
                Log(self,@"Connected!");
                isConnectedWithHost = true;
                [InfoMessages.shared onConnect];
                return remoteSocket;
            }
        }

    Log(self,@"Could not connect: %s", strerror(errno));
    close(remoteSocket);
//    NSString *message = [NSString stringWithFormat:@"%s", strerror(errno)];
     Log(self,@"%s", strerror(errno));
    [InfoMessages.shared onError];
    return 0;
}

static dispatch_queue_t writeQueue; // queue to synchronise outgoing writes
static struct _rmdevice device; // header sent to RemoteUI server on connect
static NSValue *inhibitEcho; // prevent events from server going back to server
static Class UIWindowLayer; // Use to filter for full window layer updates
static UITouch *realTouch; // An actual UITouch recycled for forging events
static CGSize bufferSize; // current size of off-screen image buffers

/// Initialse static viables for capture an swizzle in replacement
/// methods for intercepting screen updates and device events
/// Setup device header struct sent on opening the connection.
+ (void)initCapture {
    connections = [NSMutableArray new];
    timestamp0 = REPEATO_NOW;
    writeQueue = dispatch_queue_create("writeQueue", DISPATCH_QUEUE_SERIAL);
    UIWindowLayer = objc_getClass("UIWindowLayer");
    connectionKey = strdup(REPEATO_KEY.UTF8String);
    for (size_t i=0, keylen = (int)strlen(connectionKey); i<keylen; i++)
        connectionKey[i] ^= REPEATO_XOR;

    __block NSArray<UIScreen *> *screens;
    do {
        dispatch_sync(dispatch_get_main_queue(), ^{
            screens = [UIScreen screens];
        });
        if (!screens.count)
            [NSThread sleepForTimeInterval:.5];
    } while (!screens.count);

#if 01
    method_exchangeImplementations(
        class_getInstanceMethod(CALayer.class, @selector(_copyRenderLayer:layerFlags:commitFlags:)),
        class_getInstanceMethod(CALayer.class, @selector(in_copyRenderLayer:layerFlags:commitFlags:)));
#else
    method_exchangeImplementations(
        class_getInstanceMethod(CALayer.class, @selector(_didCommitLayer:)),
        class_getInstanceMethod(CALayer.class, @selector(in_didCommitLayer:)));
#endif
    method_exchangeImplementations(
        class_getInstanceMethod(UIApplication.class, @selector(sendEvent:)),
        class_getInstanceMethod(UIApplication.class, @selector(in_sendEvent:)));


    device.version = HYBRID_VERSION;

    // prepare remote header
    *(int *)device.remote.magic = REPEATO_MAGIC;

    size_t size = sizeof device.remote.machine-1;
    sysctlbyname("hw.machine", device.remote.machine, &size, NULL, 0);
    device.remote.machine[size] = '\000';

    NSDictionary *infoDict = [NSBundle mainBundle].infoDictionary;
    strncpy(device.remote.appname, [infoDict[@"CFBundleIdentifier"] UTF8String]?:"", sizeof device.remote.appname-1);
    strncpy(device.remote.appvers, [infoDict[@"CFBundleShortVersionString"] UTF8String]?:"", sizeof device.remote.appvers-1);
    
    gethostname(device.remote.hostname, sizeof device.remote.hostname-1);

    *(float *)device.remote.scale = scaleUpFactor;
    *(int *)device.remote.isIPad = [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad;
    
    *(int *)device.remote.protocolVersion = 127;
    CGRect screenBounds = [self screenBounds];
    CGSize screenSize = screenBounds.size;

    NSString *displaySize = [NSString stringWithFormat:@"%ix%i", (int) screenSize.width, (int) screenSize.height];
    strncpy(device.remote.displaySize, [displaySize UTF8String], sizeof device.remote.displaySize-1);

    
    
    __block NSString *deviceName;
    __block float systemVersion;

    dispatch_sync(dispatch_get_main_queue(), ^{
        deviceName = [[UIDevice currentDevice] name];
        NSString *systemVersionString = [[UIDevice currentDevice] systemVersion];
        systemVersion = [systemVersionString floatValue];
    });

    strncpy(device.remote.deviceName, [deviceName UTF8String], sizeof(device.remote.deviceName) - 1);
    
    //device model can't be fetched reliably
    //NSString *deviceModel = [self getDeviceModel];
    //strncpy(device.remote.deviceModel, [deviceModel UTF8String], sizeof(device.remote.deviceModel) - 1);
    
    //strncpy(device.remote.systemVersion, [systemVersion UTF8String], sizeof(device.remote.systemVersion) - 1);
    *(float *)device.remote.systemVersion = systemVersion;
    if (NSClassFromString(@"FlutterViewController") != nil) {
        // The app likely includes Flutter
        *(int *)device.remote.appFrameWorkType = 1;
        Log(self, @"AppFrameWorkType: Flutter" );
    } else if (NSClassFromString(@"ComposeAppBase") != nil || NSClassFromString(@"Applier") != nil) {
        // The app likely includes Kotlin Multiplatform Compose. In this case the _copyRenderLayer method fizzling approach will not work and therefore queueCapture would not be called. What we do instead is to call the method regularly
        *(int *)device.remote.appFrameWorkType = 2; // Adjust the value as needed
        Log(self, @"AppFrameWorkType: Compose" );
        dispatch_async(dispatch_get_main_queue(), ^{
                    REPEATO_APPNAME *instance = [self sharedInstance];
                    instance.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(displayLinkFired:)];
                    instance.displayLink.preferredFramesPerSecond = 20;
                    [instance.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
                });
    } else {
        Log(self, @"AppFrameWorkType: iOS" );
    }
}

static int skipEcho; // Was to filter out layer commits during capture
static BOOL capturing; // Am in the middle of capturing
static BOOL isStreamEnabled = true;
static float scaleUpFactor = 0.5; // can be remote controlled
static NSTimeInterval mostRecentScreenUpdate; // last window layer update
static NSTimeInterval lastCaptureTime; // last time capture was forced
static NSArray *buffers; // off-screen buffers use in encoding images
static int frameno; // count of frames captured and transmmitted

/// Best effeort to get screen dimensions, even for iOS on M1 Mac
+ (CGRect)screenBounds {
    CGRect bounds = CGRectZero;
    while (TRUE) {
        for (UIWindow *window in [UIApplication sharedApplication].windows)  {
            if (window.bounds.size.height > bounds.size.height)
                bounds = window.bounds;
        }
        if (bounds.size.height)
            break;
        else
            [[NSRunLoop mainRunLoop]
             runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.2]];
    }
    return bounds;
}

/// Capture device's screen (and discard it if there is a more recent update to the screen on the way)
/// @param timestamp Time update to the screen was notified
/// @param flush force transmission of screen update reguardless of timestamp
+ (void)capture:(NSTimeInterval)timestamp flush:(BOOL)flush {
    if(!isStreamEnabled){
        Log(self, @"Capture: disabled");
        return;
    }
    Log(self, @"Capture now! %f", scaleUpFactor);
    
    NSTimeInterval start = REPEATO_NOW;
    
    //UIScreen *screen = [UIScreen mainScreen];
    CGRect screenBounds = [self screenBounds];
    CGSize screenSize = screenBounds.size;
    CGFloat imageScale = *(int *)device.remote.isIPad ? 1. : scaleUpFactor;
    __block struct _rmframe frame = {REPEATO_NOW,
        {{(float)screenSize.width, (float)screenSize.height, (float)imageScale}}, 0};

    if (bufferSize.width != frame.width || bufferSize.height != frame.height) {
        buffers = nil;
        buffers = @[[[self alloc] initFrame:&frame],
                    [[self alloc] initFrame:&frame]];
        bufferSize = screenSize;
        frameno = 0;
    }

    REPEATO_APPNAME *buffer = buffers[frameno++&1];
    REPEATO_APPNAME *prevbuff = buffers[frameno&1];
    UIImage *screenshot;
    RMDebug(@"%@, %@ -- %@", buffers, buffer, prevbuff);
    RMBench(" pre Captured #%d(%d), %.1fms %f\n", frameno, flush, (REPEATO_NOW-start)*1000., timestamp);
  
    capturing = TRUE;
    RMDebug(@"CAPTURE0");
    

    CGRect fullBounds = CGRectMake(0, 0,
                                   screenSize.width * scaleUpFactor,
                                   screenSize.height * scaleUpFactor);
    UIGraphicsBeginImageContext(fullBounds.size);
    for (UIWindow *window in [UIApplication sharedApplication].windows)
        if (!window.isHidden){
            [window drawViewHierarchyInRect:fullBounds afterScreenUpdates:NO];
        }

    RMBench(" pre Captured #%d(%d), %.1fms %f\n", frameno, flush, (REPEATO_NOW-start)*1000., timestamp);

    screenshot = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    

    skipEcho = 0;

    RMDebug(@"CAPTURE2 %@", [UIApplication sharedApplication].windows.lastObject);
    capturing = FALSE;
    RMBench("Captured #%d(%d), %.1fms %f\n", frameno, flush,
            (REPEATO_NOW-start)*1000., timestamp);
    

    dispatch_async(writeQueue, ^{
        if (timestamp < mostRecentScreenUpdate && !flush) {
            RMBench("Discard 3 %d\n", flush);
            frameno--;
            return;
        }

        NSTimeInterval start = REPEATO_NOW;
        [self encodeAndTransmit:screenshot screenSize:screenSize
                          frame:frame buffer:buffer prevbuff:prevbuff];
        RMBench("Sent #%d(%d), %.1fms %f\n", frameno, flush, ([NSDate
                  timeIntervalSinceReferenceDate]-start)*1000., timestamp);
    });
}

/// Encode image either using Remote's run-length encoded format relaive to previous capture
/// or in minicap format (int32_t length + jpeg image format)
/// @param screenshot UIImage containing screen contents
/// @param screenSize Best extimate of screen size
/// @param frame struct  to be transmitted to server when using Remote's native format
/// @param buffer Buffer to contain most recent screenshot
/// @param prevbuff Buffer containing previous screenshot to relative encode
+ (void)encodeAndTransmit:(UIImage *)screenshot
               screenSize:(CGSize)screenSize frame:(struct _rmframe)frame
       buffer:(REPEATO_APPNAME *)buffer prevbuff:(REPEATO_APPNAME *)prevbuff
{
        NSData *encoded = UIImageJPEGRepresentation(screenshot, REPEATO_JPEGQUALITY);
      
    
        //Log(self, @" frame size: %lu", encoded.length);

        for (NSValue *fp in connections) {
            FILE *writeFp = (FILE *)fp.pointerValue;
            uint32_t frameSize = (uint32_t)encoded.length;
            int frameHeaderSize = device.version <= HYBRID_VERSION ?
                                    sizeof frameSize : sizeof frame;
            if (fwrite(device.version <= HYBRID_VERSION ? (void *)&frameSize :
                      (void *)&frame, 1, frameHeaderSize, writeFp) != frameHeaderSize)
                Log(self, @"Could not write frame: %s", strerror(errno));
            else if (fwrite(encoded.bytes, 1, encoded.length, writeFp) != encoded.length)
                Log(self, @"Could not write encoded: %s", strerror(errno));
            else
                fflush(writeFp);
        }
}

/// Finds the first descendant view of a given class name in a view hierarchy.
/// @param view View to search through
/// @param className Name of the class to search for
+ (UIView *)findDescendantViewInView:(UIView *)view withClassName:(NSString *)className {
    if ([NSStringFromClass([view class]) isEqualToString:className]) {
        return view;
    }
    for (UIView *subview in view.subviews) {
        UIView *result = [self findDescendantViewInView:subview withClassName:className];
        if (result) {
            return result;
        }
    }
    return nil;
}

/// Injects text into webview's HTML document. Must 'click'/focus on input before executing this function.
/// @param text Text to be injected
/// @param webView Web view to inject text into
+ (void)injectText:(NSString *)text intoWebView:(WKWebView *)webView {
    NSString *js = [NSString stringWithFormat:
            @"document.execCommand('insertText', false, '%@')", text];

    [webView evaluateJavaScript:js completionHandler:^(id result, NSError *error) {
        if (error) {
            NSLog(@"Error injecting text: %@", error.localizedDescription);
        } else {
            NSLog(@"Text injected successfully");
        }
    }];
}

/// Run in backgrount to process event structs coming from user interface in order to forge them
/// @param writeFp Connection to RemoteUI server
+ (void)processEvents:(NSValue *)writeFp {
    FILE *readFp = fdopen(fileno((FILE *)writeFp.pointerValue), "r");

    struct _rmevent rpevent;
    while (fread(&rpevent, 1, sizeof rpevent, readFp) == sizeof rpevent) {

        RMLog(@"%@ Event: %d (%f %f)", self, rpevent.phase, rpevent.touches[0].x, rpevent.touches[0].y);

        //if (rpevent.phase == RMTouchMoved && capturing)
        //    continue;

        NSTimeInterval timestamp = rpevent.timestamp;

        NSString *sentText;
        if (rpevent.phase >= RMTouchInsertText) {
            size_t textSize = rpevent.phase - RMTouchInsertText;
            char *buffer = (char *)malloc(textSize + 1);
            textSize = fread(buffer, 1, textSize, readFp);
            buffer[textSize] = '\000';
            sentText = [NSString stringWithUTF8String:buffer];
            free(buffer);
        }

        dispatch_sync(dispatch_get_main_queue(), ^{
            CGPoint location = {rpevent.touches[0].x, rpevent.touches[0].y},
                location2 = {rpevent.touches[1].x, rpevent.touches[1].y};
            static UITextAutocorrectionType saveAuto;
            static BOOL isTextfield, isKeyboard, isButton;
            static UITextField *textField;
            static UITouch *currentTouch2;
            static UIView *currentTarget;
            static unsigned touchIdentifier = 120;
            static WKWebView *webView;
            touchIdentifier++;

            static UITouchesEvent *event;
            if (!event)
                event = [[objc_getClass("UITouchesEvent") alloc] _init];
            RCFakeEvent *fakeEvent = [RCFakeEvent new];
            fakeEvent->_timestamp = timestamp;

            if (sentText) {
                if ([sentText isEqualToString:@"repeato:clear_text"]){
                    Log(self, @"Remote asked to clear text");
                    [textField setText:@""];
                } else if ([sentText isEqualToString:@"repeato:backspace"]) {
                    Log(self, @"Remote asked to remove last letter");
                    [textField deleteBackward];
                } else if ([sentText isEqualToString:@"repeato:quit_app"]) {
                    Log(self, @"Remote asked to quit app");
                    exit(0);
                } else if ([sentText isEqualToString:@"repeato:enable_stream"]) {
                    Log(self, @"Enable image stream");
                    isStreamEnabled = TRUE;
                } else if ([sentText isEqualToString:@"repeato:disable_stream"]) {
                    Log(self, @"Disable image stream");
                    isStreamEnabled = FALSE;
                } else if ([sentText hasPrefix:@"repeato:set_scale_up_factor:"]) {
                    NSString *scaleUpParam = [sentText stringByReplacingOccurrencesOfString:@"repeato:set_scale_up_factor:" withString:@""];
                    Log(self, @"Set scale up factor to %{public}@", scaleUpParam);
                    scaleUpFactor = [scaleUpParam floatValue];
                    // send a new frame with the requested resolution right away
                    [self queueCapture];
                } else if ([sentText isEqualToString:@"\\n"]) {
                    // Trigger text input `Enter` key event
                    if (textField != nil && [textField.delegate respondsToSelector:@selector(textFieldShouldReturn:)]) {
                        Log(self, @"Enter key");
                        [textField.delegate textFieldShouldReturn:textField];
                    }
                } else {
                    if (textField != nil) {
                        Log(self, @"Insert text: %@", sentText);
                        [textField insertText:sentText];
                    } else if (webView != nil) {
                        Log(self, @"Insert text into webview");
                        [self injectText:sentText intoWebView:webView];
                    } else {
                        Log(self, @"No textfield or webview found");
                    }
                }

                return;
            }

            inhibitEcho = writeFp;

            switch (rpevent.phase) {

                case RMTouchBeganDouble:

                    currentTarget = nil;
                    for (UIWindow *window in [UIApplication sharedApplication].windows) {
                        UIView *found = [window hitTest:location2 withEvent:nil];
                        if (found)
                            currentTarget = found;
                    }
                    

                    RMDebug(@"Double Target selected: %@", currentTarget);

                    currentTouch2 = [UITouch new];

                    [currentTouch2 setTimestamp:timestamp];
                    [currentTouch2 setInitialTouchTimestamp:timestamp];
                    [currentTouch2 setPhase:(UITouchPhase)rpevent.phase];
                    [currentTouch2 setWindow:currentTarget.window];
                    [currentTouch2 _setTouchIdentifier:touchIdentifier];
                    [currentTouch2 _setPathIndex:1];
                    [currentTouch2 _setPathIdentity:2];
                    [currentTouch2 _setType:0];
                    [currentTouch2 _setSenderID:778835616971358211];
                    [currentTouch2 _setZGradient:0.0];
                    [currentTouch2 _setEdgeType:0];
                    [currentTouch2 _setEdgeAim:0];

                    [currentTouch2 setView:currentTarget];
                    [currentTouch2 setTapCount:1];
                    [currentTouch2 _setLocationInWindow:location resetPrevious:YES];

                case RMTouchBegan:
                    currentTarget = nil;
                    
                    for (UIWindow *window in [UIApplication sharedApplication].windows) {
                        UIView *found = [window hitTest:location withEvent:fakeEvent];
                        if (found)
                            currentTarget = found;
                        
                        /// Find WebView in the window
                        UIView *foundWebView = [self findDescendantViewInView:window withClassName:@"RNCWKWebView"];
                        if (foundWebView) {
                            Log(self, @"Found webview: %@", foundWebView);
                            webView = (WKWebView *)foundWebView;
                        }
                    }

                    isTextfield = [currentTarget respondsToSelector:@selector(setAutocorrectionType:)];
                    isKeyboard = [currentTarget isKindOfClass:objc_getClass("UIKeyboardLayoutStar")];

                    static NSArray *needsTouch;
                    if (!needsTouch) needsTouch = @[
                        objc_getClass("UIButton"),
                        objc_getClass("UITabBarButton"),
                        objc_getClass("_UIButtonBarButton"),
                        objc_getClass("UISegmentedControl"),
                        objc_getClass("UITableViewCellEditControl"),
                        objc_getClass("UISwipeActionStandardButton"),
                    ];

                    isButton = [currentTarget class] == [UIView class];
                    for (Class buttonClass in needsTouch)
                        if ([currentTarget isKindOfClass:buttonClass])
                            isButton = true;

                    if (!currentTouch)
                        currentTouch = realTouch ?: [UITouch new];
                    [currentTouch _setTouchIdentifier:touchIdentifier];

                    RMDebug(@"Target selected: %@ %d %d %d %d %lx\n%@",
                            currentTarget, isTextfield, isKeyboard, isButton,
                            currentTouch._touchIdentifier, currentTouch.hash,
                            [currentTarget recursiveDescription]);

                    if (currentTarget.superview.class ==
                        objc_getClass("UIKeyboardEmojiCollectionViewCell")) {
                        NSString *emoji = [(UILabel *)currentTarget.subviews[0] text];
                        [textField insertText:emoji];
                    }

                    if (isTextfield) {
                        textField = (UITextField *)currentTarget;
                        saveAuto = textField.autocorrectionType;
                        textField.autocorrectionType = UITextAutocorrectionTypeNo;
                    }

                    [currentTouch setTimestamp:timestamp];
                    [currentTouch setInitialTouchTimestamp:timestamp];
                    [currentTouch setPhase:(UITouchPhase)rpevent.phase];
                    [currentTouch setWindow:currentTarget.window];
                    [currentTouch _setPathIndex:1];
                    [currentTouch _setPathIdentity:2];
                    [currentTouch _setType:0];
                    [currentTouch _setSenderID:778835616971358211];
                    [currentTouch _setZGradient:0.0];
                    [currentTouch _setEdgeType:0];
                    [currentTouch _setEdgeAim:0];
                    [currentTouch setView:currentTarget];
                    [currentTouch setTapCount:1];
                    [currentTouch _setLocationInWindow:location resetPrevious:YES];

                    currentTouches = [NSSet setWithObjects:currentTouch, currentTouch2, nil];

                    [event _clearTouches];
                    [event _addTouch:currentTouch forDelayedDelivery:NO];
                    if (currentTouch2)
                        [event _addTouch:currentTouch2 forDelayedDelivery:NO];

                    [[UIApplication sharedApplication] in_sendEvent:event];
                    if (isButton)
                        [currentTarget touchesBegan:currentTouches withEvent:fakeEvent];
                    break;

                case RMTouchMoved:
                case RMTouchStationary:
                    [currentTouch setPhase:(UITouchPhase)rpevent.phase];
                    [currentTouch _setLocationInWindow:location resetPrevious:YES];
                    [currentTouch setTimestamp:timestamp];

                    [currentTouch2 setPhase:(UITouchPhase)rpevent.phase];
                    [currentTouch2 _setLocationInWindow:location2 resetPrevious:YES];
                    [currentTouch2 setTimestamp:timestamp];

                    [[UIApplication sharedApplication] in_sendEvent:event];
                    if (isButton)
                        [currentTarget touchesMoved:currentTouches withEvent:fakeEvent];
                    break;

                case RMTouchEnded:
                case RMTouchCancelled:
                    [currentTouch setPhase:(UITouchPhase)rpevent.phase];
                    [currentTouch _setLocationInWindow:location resetPrevious:YES];
                    [currentTouch setTimestamp:timestamp];

                    [currentTouch2 setPhase:(UITouchPhase)rpevent.phase];
                    [currentTouch2 _setLocationInWindow:location2 resetPrevious:YES];
                    [currentTouch2 setTimestamp:timestamp];

                    [[UIApplication sharedApplication] in_sendEvent:event];
                    if (isButton)
                        [currentTarget touchesEnded:currentTouches withEvent:fakeEvent];

                    if (isTextfield) {
                        UITextField *textField = (UITextField *)currentTarget;
                        textField.autocorrectionType = saveAuto;
                    }

                    currentTouches = nil;
                    currentTarget = nil;
                    currentTouch2 = nil;
                    currentTouch = nil;
                    event = nil;
                    break;

                default:
                    Log(self, @"Invalid Event: %d", rpevent.phase);
            }

            inhibitEcho = nil;
        });
    }

    Log(self, @"processEvents: exits");
    fclose(readFp);

    [connections removeObject:writeFp];
    fclose((FILE *)writeFp.pointerValue);
    if (!connections.count)
        [self shutdown];
    Log(self, @"Disconnected");
    isConnectedWithHost = false;
    [InfoMessages.shared onDisconnect];
}

/// Stop capturing events
+ (void)shutdown {
    [repeatoDelegate remoteConnected:FALSE];
    for (NSValue *writeFp in connections)
        fclose((FILE *)writeFp.pointerValue);
    connections = nil;
    // Invalidate the display link
    REPEATO_APPNAME *instance = [self sharedInstance];
    [instance.displayLink invalidate];
    instance.displayLink = nil;
}

/// A delicate peice of code to work out when to request the capture of the screen
/// and transmission of it's representation to the RemoteUI server. Routed through
/// writeQueue to ensure that output does not back up on say, cellular connections.
+ (void)queueCapture {
    if (!connections.count)
        return;

    NSTimeInterval timestamp = mostRecentScreenUpdate = REPEATO_NOW;
    // 1. if a minimum time (REPEATO_MAXDEFER) passed since transmitting the last frame -> schedule flush
    BOOL flush = timestamp > lastCaptureTime + REPEATO_MAXDEFER;
    if (flush)
        lastCaptureTime = timestamp;
    
    dispatch_async(writeQueue, ^{
        int64_t delta = 0;
        if (!flush) {
            if (timestamp < mostRecentScreenUpdate) {
                Log(self, @"Discard 1 flush: %d", flush);
                return;
            }
            
            delta = (int64_t)(REPEATO_DEFER * NSEC_PER_SEC);
        }

        // 2. wait another x ms (REPEATO_DEFER) to figure out if maybe a newer frame is coming in. In that case, the prev frame is outdated and can be discarded
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delta), dispatch_get_main_queue(), ^{
            if(flush){
                if(timestamp < lastCaptureTime){
                    Log(self, @"timestamp < lastCaptureTime");
                    return;
                }
            }else {
                if(timestamp < mostRecentScreenUpdate){
                    Log(self, @"timestamp < mostRecentScreenUpdate");
                    return;
                }
            }
            
            [self capture:timestamp flush:flush];
//            lastCaptureTime = timestamp;
        });
    });
}


+ (void)displayLinkFired:(CADisplayLink *)displayLink {
    [self queueCapture];
}

+ (instancetype)sharedInstance {
    static REPEATO_APPNAME *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[self alloc] init];
    });
    return sharedInstance;
}

@end

@implementation CALayer(REPEATO_APPNAME)

/// Twp methods that can be swizzled in to generate a stream of notifications the screen has been updated
- (void *)in_copyRenderLayer:(void *)a0 layerFlags:(unsigned)a1 commitFlags:(unsigned *)a2 {
    void *out = [self in_copyRenderLayer:a0 layerFlags:a1 commitFlags:a2];
    RMDebug(@"in_copyRenderLayer: %d %d %@ %lu", capturing, skipEcho, self,
            (unsigned long)[UIApplication sharedApplication].windows.count);
    if (self.class == UIWindowLayer)
        [REPEATO_APPNAME queueCapture];
    return out;
}

- (void)in_didCommitLayer:(void *)a0 {
    [self in_didCommitLayer:a0];
    RMDebug(@"in_didCommitLayer: %d %d %@ %lu", capturing, skipEcho, self,
            (unsigned long)[UIApplication sharedApplication].windows.count);
    if (self.class == UIWindowLayer)
        [REPEATO_APPNAME queueCapture];
}

@end

@implementation UIApplication(REPEATO_APPNAME)

/// Swizzled in to capture device events and transmit them to the RemoteUI server
/// so they can be recorded and played back using processEvents: above.
/// Events encoded as a fake frame with negative length rather than image & size.
/// @param anEvent actual UIEvent which contains the UITouches
- (void)in_sendEvent:(UIEvent *)anEvent {
    [self in_sendEvent:anEvent];
    NSSet *touches = anEvent.allTouches;
    NSValue *incomingFp = inhibitEcho;
    realTouch = touches.anyObject;

//    RMLog(@"%@", anEvent);
//    for (UITouch *t in touches)
//        RMLog(@"Gestures: %@", t.gestureRecognizers);


    struct _rmframe header;
    header.timestamp = REPEATO_NOW;
    header.length = -(int)touches.count;

    NSMutableData *out = [NSMutableData new];
    if (device.version <= HYBRID_VERSION)
        [out appendBytes:&header.length length:sizeof header.length];

    for (UITouch *touch in touches) {
        CGPoint loc = [touch locationInView:touch.window];
        header.phase = (RMTouchPhase)touch.phase;
        header.x = loc.x;
        header.y = loc.y;
        [out appendBytes:&header length:sizeof header];
        header.length++;
    }

    dispatch_async(writeQueue, ^{
        for (NSValue *fp in connections) {
            if (fp == incomingFp)
                continue;
            FILE *writeFp = (FILE *)fp.pointerValue;
            if (fwrite(out.bytes, 1, out.length, writeFp) != out.length)
                Log(self, @"Could not write event: %s", REPEATO_APPNAME.class, strerror(errno));
            else
                fflush(writeFp);
        }
    });
}

#endif
@end
#endif
