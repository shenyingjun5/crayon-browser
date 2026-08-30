#include "macos/page_markdown_platform_mac.h"

#import <AppKit/AppKit.h>

namespace crayon::browser::cef_shell::macos {

bool CopyMarkdownToPasteboard(const std::string& markdown) {
  if (markdown.empty()) return false;
  @autoreleasepool {
    NSString* value = [[NSString alloc] initWithBytes:markdown.data()
                                               length:markdown.size()
                                             encoding:NSUTF8StringEncoding];
    if (!value) return false;
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    return [pasteboard setString:value forType:NSPasteboardTypeString];
  }
}

}  // namespace crayon::browser::cef_shell::macos
