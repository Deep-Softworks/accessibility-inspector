#ifndef AXTRACE_AX_H
#define AXTRACE_AX_H

typedef int pid_t;
typedef unsigned int uint32_t;

/* Opaque refs - avoid including Apple headers that use CFSTR */
typedef const struct __AXUIElement* AXUIElementRef;
typedef const void* CFTypeRef;
typedef const struct __CFString* CFStringRef;
typedef const struct __CFArray* CFArrayRef;
typedef const struct __CFBoolean* CFBooleanRef;
typedef const struct __CFNumber* CFNumberRef;
typedef struct __AXValue* AXValueRef;

typedef long CFIndex;
typedef struct { CFIndex location; CFIndex length; } CFRange;

/* AX error code */
#define kAXErrorSuccess 0
#define kCFCompareEqualTo 0
#define kCFStringEncodingUTF8 0x08000100
#define kCFNumberLongLongType 9
#define kCFNumberDoubleType 13
#define kAXValueCFRangeType 2

/* Accessibility API */
int AXIsProcessTrusted(void);
AXUIElementRef AXUIElementCreateApplication(pid_t pid);
int AXUIElementCopyAttributeNames(AXUIElementRef element, CFArrayRef* names);
int AXUIElementCopyAttributeValue(AXUIElementRef element, CFStringRef attribute, CFTypeRef* value);
int AXUIElementGetAttributeValueCount(AXUIElementRef element, CFStringRef attribute, CFIndex* count);
int AXUIElementCopyAttributeValues(AXUIElementRef element, CFStringRef attribute, CFIndex index, CFIndex max_values, CFArrayRef* values);

/* AXValue */
int AXValueGetType(AXValueRef value);
int AXValueGetValue(AXValueRef value, int type, void* value_ptr);

/* CoreFoundation - minimal declarations */
void CFRelease(CFTypeRef cf);
CFIndex CFArrayGetCount(CFArrayRef array);
const void* CFArrayGetValueAtIndex(CFArrayRef array, CFIndex index);
int CFStringCompare(CFStringRef s1, CFStringRef s2, uint32_t options);
uint32_t CFGetTypeID(CFTypeRef cf);
uint32_t CFStringGetTypeID(void);
uint32_t CFBooleanGetTypeID(void);
uint32_t CFNumberGetTypeID(void);
CFIndex CFStringGetLength(CFStringRef s);
CFIndex CFStringGetMaximumSizeForEncoding(CFIndex length, uint32_t encoding);
int CFStringGetCString(CFStringRef s, char* buffer, CFIndex buffer_size, uint32_t encoding);
int CFBooleanGetValue(CFBooleanRef b);
int CFNumberGetValue(CFNumberRef number, int type, void* value_ptr);
uint32_t AXValueGetTypeID(void);

/* Create CFString from C string - caller must CFRelease result */
CFStringRef CFStringCreateWithCString(void* allocator, const char* cstring, uint32_t encoding);

#endif
