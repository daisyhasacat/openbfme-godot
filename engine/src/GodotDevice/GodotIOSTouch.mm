// OpenBFME. GPL-3.0.
// UIKit preserves UITouch.type, which Godot's stock screen-touch events omit.
// Intercept only the public UIResponder touch methods on Godot's rendering view.
#include "GodotDevice/GodotIOSTouch.h"
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cmath>

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
        for (int phase = 0; phase < 4; ++phase) {
            auto &hook = hooks[phase];
            const SEL selector = sel_registerName(hook.name);
            Method method = class_getInstanceMethod(view_class, selector);
            hook.original = method_getImplementation(method);
            hook.replacement = imp_implementationWithBlock(^(UIView *view, NSSet *touches, UIEvent *event) {
                NSMutableSet *fingers = [NSMutableSet setWithCapacity:touches.count];
                for (UITouch *touch in touches) {
                    if (touch.type == UITouchTypePencil) emit_pencil(view, touch, phase);
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
        view_class = Nil;
    });
}
