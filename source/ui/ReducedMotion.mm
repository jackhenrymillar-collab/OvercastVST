#include "ReducedMotion.h"

#import <AppKit/AppKit.h>

namespace overcast::ui
{

bool prefersReducedMotion()
{
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}

} // namespace overcast::ui
