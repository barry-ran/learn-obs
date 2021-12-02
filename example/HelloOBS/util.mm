#include "util.h"
#import <AppKit/AppKit.h>

Util::Util()
{

}

bool Util::is_in_bundle()
{
    NSRunningApplication *app = [NSRunningApplication currentApplication];
    return [app bundleIdentifier] != nil;
}
