// OpenBFME. GPL-3.0.
// UIKit preserves UITouch.type, which Godot's stock screen-touch events omit.
// Indirect pointer gestures use public UIKit APIs (Apple WWDC20, Handle trackpad and mouse input).
// Intercept only the public UIResponder touch methods on Godot's rendering view.
#include "GodotDevice/GodotIOSTouch.h"
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cmath>
#include <godot_cpp/classes/input_event_pan_gesture.hpp>
#include <godot_cpp/classes/input_event_magnify_gesture.hpp>

// UIKit pointer input must remain mouse input, independent of direct touch controls.
@interface OpenBFMETrackpad : NSObject <UIGestureRecognizerDelegate>
@property(nonatomic, weak) UIView *view;
@property(nonatomic) NSUInteger buttons;
@property(nonatomic) CGPoint last;
@property(nonatomic) BOOL hasLast;
@property(nonatomic, strong) NSMutableArray<UIGestureRecognizer *> *recognizers;
- (void)hover:(UIHoverGestureRecognizer *)gesture;
- (void)scroll:(UIPanGestureRecognizer *)gesture;
- (void)pinch:(UIPinchGestureRecognizer *)gesture;
@end

static const void *trackpadKey = &trackpadKey;
static godot::Vector2 pointerPoint(UIView *view, CGPoint point) {
    return godot::Vector2(point.x * view.contentScaleFactor, point.y * view.contentScaleFactor);
}
static void pointerModifiers(godot::InputEventWithModifiers *event, UIKeyModifierFlags flags) {
    event->set_shift_pressed((flags & UIKeyModifierShift) != 0);
    event->set_ctrl_pressed((flags & UIKeyModifierControl) != 0);
    event->set_alt_pressed((flags & UIKeyModifierAlternate) != 0);
    event->set_meta_pressed((flags & UIKeyModifierCommand) != 0);
}
@implementation OpenBFMETrackpad
- (void)hover:(UIHoverGestureRecognizer *)gesture {
    auto *input = godot::Input::get_singleton();
    if (!input) return;
    const CGPoint point = [gesture locationInView:self.view];
    godot::Ref<godot::InputEventMouseMotion> motion;
    motion.instantiate();
    pointerModifiers(motion.ptr(), gesture.modifierFlags);
    motion->set_position(pointerPoint(self.view, point));
    motion->set_global_position(motion->get_position());
    if (self.hasLast) motion->set_relative(pointerPoint(self.view, CGPointMake(point.x-self.last.x, point.y-self.last.y)));
    motion->set_screen_relative(motion->get_relative());
    motion->set_button_mask(godot::BitField<godot::MouseButtonMask>(self.buttons));
    input->parse_input_event(motion);
    self.last = point;
    self.hasLast = gesture.state != UIGestureRecognizerStateEnded && gesture.state != UIGestureRecognizerStateCancelled;
}
- (void)scroll:(UIPanGestureRecognizer *)gesture {
    const CGPoint delta = [gesture translationInView:self.view];
    [gesture setTranslation:CGPointMake(0, 0) inView:self.view];
    if (!godot::Input::get_singleton() || (delta.x == 0 && delta.y == 0)) return;
    godot::Ref<godot::InputEventPanGesture> event;
    event.instantiate();
    pointerModifiers(event.ptr(), gesture.modifierFlags);
    event->set_position(pointerPoint(self.view, [gesture locationInView:self.view]));
    // Match Godot macOS scrollWheel/processPanEvent: precise point deltas * 0.03, opposite content movement.
    event->set_delta(godot::Vector2(-delta.x * 0.03, -delta.y * 0.03));
    godot::Input::get_singleton()->parse_input_event(event);
}
- (void)pinch:(UIPinchGestureRecognizer *)gesture {
    const double factor = gesture.scale;
    gesture.scale = 1;
    if (!godot::Input::get_singleton() || !std::isfinite(factor) || factor <= 0 || factor == 1) return;
    godot::Ref<godot::InputEventMagnifyGesture> event;
    event.instantiate();
    pointerModifiers(event.ptr(), gesture.modifierFlags);
    event->set_position(pointerPoint(self.view, [gesture locationInView:self.view]));
    event->set_factor(factor);
    godot::Input::get_singleton()->parse_input_event(event);
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)a shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)b { return YES; }
@end

namespace {
using TouchMethod = void (*)(id, SEL, NSSet *, UIEvent *);
struct Hook {
    const char *name;
    IMP original = nullptr;
    IMP replacement = nullptr;
};
Hook hooks[] = {{"touchesBegan:withEvent:"}, {"touchesMoved:withEvent:"},
    {"touchesEnded:withEvent:"}, {"touchesCancelled:withEvent:"}};
Class view_class = Nil;

IMP originalMovedToWindow = nullptr;
IMP movedToWindowHook = nullptr;
NSHashTable<UIView *> *pointerViews;
void install_pointer(UIView *view) {
    if (objc_getAssociatedObject(view, trackpadKey)) return;
    OpenBFMETrackpad *bridge = [OpenBFMETrackpad new];
    bridge.view = view;
    UIHoverGestureRecognizer *hover = [[UIHoverGestureRecognizer alloc] initWithTarget:bridge action:@selector(hover:)];
    hover.allowedTouchTypes = @[@(UITouchTypeIndirectPointer)];
    UIPanGestureRecognizer *scroll = [[UIPanGestureRecognizer alloc] initWithTarget:bridge action:@selector(scroll:)];
    scroll.allowedTouchTypes = @[]; // Scroll events only; fingers keep the existing touch controls.
    scroll.allowedScrollTypesMask = UIScrollTypeMaskContinuous;
    UIPinchGestureRecognizer *pinch = [[UIPinchGestureRecognizer alloc] initWithTarget:bridge action:@selector(pinch:)];
    pinch.allowedTouchTypes = @[@(UITouchTypeIndirectPointer)];
    bridge.recognizers = [NSMutableArray arrayWithArray:@[hover, scroll, pinch]];
    for (UIGestureRecognizer *gesture in bridge.recognizers) {
        gesture.cancelsTouchesInView = NO;
        gesture.delaysTouchesBegan = NO;
        gesture.delaysTouchesEnded = NO;
        gesture.delegate = bridge;
        [view addGestureRecognizer:gesture];
    }
    objc_setAssociatedObject(view, trackpadKey, bridge, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [pointerViews addObject:view];
}
void find_pointer_views(UIView *view) {
    if ([view isKindOfClass:view_class]) install_pointer(view);
    for (UIView *child in view.subviews) find_pointer_views(child);
}
void emit_pointer(UIView *view, UITouch *touch, UIEvent *native, int phase) {
    install_pointer(view);
    OpenBFMETrackpad *bridge = objc_getAssociatedObject(view, trackpadKey);
    auto *input = godot::Input::get_singleton();
    if (!input) return;
    const CGPoint point = [touch locationInView:view];
    // UIKit primary/secondary bits match Godot's left/right button masks.
    const NSUInteger next = OpenBFMEIOSTouch::pointerButtons(native.buttonMask, bridge.buttons, phase);
    const NSUInteger changed = bridge.buttons ^ next;
    bridge.last = point; bridge.hasLast = YES;
    if (phase == 1) {
        godot::Ref<godot::InputEventMouseMotion> motion;
        motion.instantiate();
        pointerModifiers(motion.ptr(), native.modifierFlags);
        motion->set_position(pointerPoint(view, point));
        motion->set_global_position(motion->get_position());
        const CGPoint previous = [touch previousLocationInView:view];
        motion->set_relative(pointerPoint(view, CGPointMake(point.x-previous.x, point.y-previous.y)));
        motion->set_screen_relative(motion->get_relative());
        motion->set_button_mask(godot::BitField<godot::MouseButtonMask>(next));
        input->parse_input_event(motion);
    }
    for (int index = 1; index <= 3; ++index) {
        const NSUInteger mask = 1u << (index-1);
        if (!(changed & mask)) continue;
        godot::Ref<godot::InputEventMouseButton> button;
        button.instantiate();
        pointerModifiers(button.ptr(), native.modifierFlags);
        button->set_position(pointerPoint(view, point));
        button->set_global_position(button->get_position());
        button->set_button_index(static_cast<godot::MouseButton>(index));
        button->set_button_mask(godot::BitField<godot::MouseButtonMask>(next));
        button->set_pressed((next & mask) != 0);
        button->set_double_click((next & mask) && touch.tapCount > 1);
        button->set_canceled(phase == 3);
        input->parse_input_event(button);
    }
    bridge.buttons = next;
}

void emit_pencil(UIView *view, UITouch *touch, int phase) {
    auto *input = godot::Input::get_singleton();
    if (!input) return;
    const CGPoint point = [touch locationInView:view];
    const CGFloat scale = view.contentScaleFactor;
    const godot::Vector2 position(point.x * scale, point.y * scale);
    if (phase == 1) {
        godot::Ref<godot::InputEventMouseMotion> motion;
        motion.instantiate();
        motion->set_device(OpenBFMEIOSTouch::PencilDevice);
        motion->set_position(position);
        motion->set_global_position(position);
        motion->set_button_mask(godot::MOUSE_BUTTON_MASK_LEFT);
        const CGPoint previous = [touch previousLocationInView:view];
        const godot::Vector2 relative((point.x - previous.x) * scale, (point.y - previous.y) * scale);
        motion->set_relative(relative);
        motion->set_screen_relative(relative);
        motion->set_pressure(touch.maximumPossibleForce > 0 ? touch.force / touch.maximumPossibleForce : 0);
        const CGVector azimuth = [touch azimuthUnitVectorInView:view];
        const double tilt = std::cos(touch.altitudeAngle);
        motion->set_tilt(godot::Vector2(azimuth.dx * tilt, azimuth.dy * tilt));
        input->parse_input_event(motion);
    } else {
        godot::Ref<godot::InputEventMouseButton> button;
        button.instantiate();
        button->set_device(OpenBFMEIOSTouch::PencilDevice);
        button->set_position(position);
        button->set_global_position(position);
        button->set_button_index(godot::MOUSE_BUTTON_LEFT);
        button->set_button_mask(phase == 0 ? godot::MOUSE_BUTTON_MASK_LEFT : godot::BitField<godot::MouseButtonMask>(0));
        button->set_pressed(phase == 0);
        button->set_double_click(phase == 0 && touch.tapCount > 1);
        button->set_canceled(phase == 3);
        input->parse_input_event(button);
    }
}

void on_main(dispatch_block_t action) {
    if ([NSThread isMainThread]) action();
    else dispatch_sync(dispatch_get_main_queue(), action);
}
}

void OpenBFMEIOSTouch::install() {
    on_main(^{
        if (view_class) return;
        // Godot 4.7 renamed GodotView to GDTView. Both are public ObjC runtime
        // class names in the stock engine, not private UIKit APIs.
        Class candidate = NSClassFromString(@"GDTView");
        if (!candidate) candidate = NSClassFromString(@"GodotView");
        if (!candidate) {
            godot::UtilityFunctions::push_error("Apple Pencil input bridge: Godot rendering view class is unavailable.");
            return;
        }
        for (auto &hook : hooks) {
            if (!class_getInstanceMethod(candidate, sel_registerName(hook.name))) {
                godot::UtilityFunctions::push_error("Apple Pencil input bridge: Godot rendering view has incompatible touch methods.");
                return;
            }
        }
        view_class = candidate;
        pointerViews = [NSHashTable weakObjectsHashTable];
        const SEL moved = @selector(didMoveToWindow);
        Method movedMethod = class_getInstanceMethod(candidate, moved);
        originalMovedToWindow = method_getImplementation(movedMethod);
        movedToWindowHook = imp_implementationWithBlock(^(UIView *view) {
            reinterpret_cast<void (*)(id, SEL)>(originalMovedToWindow)(view, moved);
            if (view.window) install_pointer(view);
        });
        class_replaceMethod(candidate, moved, movedToWindowHook, method_getTypeEncoding(movedMethod));
        for (UIScene *scene in UIApplication.sharedApplication.connectedScenes)
            if ([scene isKindOfClass:UIWindowScene.class])
                for (UIWindow *window in ((UIWindowScene *)scene).windows) find_pointer_views(window);
        for (int phase = 0; phase < 4; ++phase) {
            auto &hook = hooks[phase];
            const SEL selector = sel_registerName(hook.name);
            Method method = class_getInstanceMethod(view_class, selector);
            hook.original = method_getImplementation(method);
            hook.replacement = imp_implementationWithBlock(^(UIView *view, NSSet *touches, UIEvent *event) {
                NSMutableSet *fingers = [NSMutableSet setWithCapacity:touches.count];
                for (UITouch *touch in touches) {
                    if (touch.type == UITouchTypePencil) emit_pencil(view, touch, phase);
                    else if (touch.type == UITouchTypeIndirectPointer) emit_pointer(view, touch, event, phase);
                    else [fingers addObject:touch];
                }
                if (fingers.count) reinterpret_cast<TouchMethod>(hooks[phase].original)(view, selector, fingers, event);
            });
            // class_replaceMethod also handles an inherited implementation
            // without modifying UIResponder's method for unrelated UIKit views.
            class_replaceMethod(view_class, selector, hook.replacement, method_getTypeEncoding(method));
        }
    });
}

void OpenBFMEIOSTouch::uninstall() {
    on_main(^{
        if (!view_class) return;
        for (auto &hook : hooks) {
            const SEL selector = sel_registerName(hook.name);
            Method method = class_getInstanceMethod(view_class, selector);
            if (method_getImplementation(method) == hook.replacement) {
                class_replaceMethod(view_class, selector, hook.original, method_getTypeEncoding(method));
                imp_removeBlock(hook.replacement);
            } else {
                // Another component has wrapped us; keep the block alive until
                // process shutdown rather than leave its method chain dangling.
                godot::UtilityFunctions::push_error("Apple Pencil input bridge: touch method changed before shutdown; retaining hook.");
            }
        }
        for (UIView *view in pointerViews) {
            OpenBFMETrackpad *bridge = objc_getAssociatedObject(view, trackpadKey);
            for (UIGestureRecognizer *gesture in bridge.recognizers) [view removeGestureRecognizer:gesture];
            objc_setAssociatedObject(view, trackpadKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        pointerViews = nil;
        const SEL moved = @selector(didMoveToWindow);
        Method method = class_getInstanceMethod(view_class, moved);
        if (method_getImplementation(method) == movedToWindowHook) {
            class_replaceMethod(view_class, moved, originalMovedToWindow, method_getTypeEncoding(method));
            imp_removeBlock(movedToWindowHook);
        }
        view_class = Nil;
    });
}
