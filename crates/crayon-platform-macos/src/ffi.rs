//! Security.framework / CoreFoundation FFI boundary (M04a).
//!
//! The single unsafe block home for the crate.  Every block carries a
//! SAFETY comment; the exposed surface is a small, fully safe keychain
//! API over generic-password items.

use std::ffi::c_void;
use std::ptr;

/// Security.framework status codes we map explicitly.
pub(crate) const ERR_SEC_SUCCESS: i32 = 0;
pub(crate) const ERR_SEC_ITEM_NOT_FOUND: i32 = -25300;
pub(crate) const ERR_SEC_AUTH_FAILED: i32 = -25291;
pub(crate) const ERR_SEC_INTERACTION_NOT_ALLOWED: i32 = -25308;
pub(crate) const ERR_SEC_ACCESS_DENIED: i32 = -60005;

/// `kCFStringEncodingUTF8` — a compile-time enum constant, not a symbol.
const K_CF_STRING_ENCODING_UTF8: u32 = 0x0800_0100;

#[link(name = "CoreFoundation", kind = "framework")]
extern "C" {
    fn CFDataCreate(allocator: *const c_void, bytes: *const u8, length: isize) -> *const c_void;
    fn CFDataGetBytePtr(data: *const c_void) -> *const u8;
    fn CFDataGetLength(data: *const c_void) -> isize;

    fn CFStringCreateWithCString(
        allocator: *const c_void,
        c_str: *const u8,
        encoding: u32,
    ) -> *const c_void;
    fn CFDictionaryCreateMutable(
        allocator: *const c_void,
        capacity: isize,
        key_callbacks: *const c_void,
        value_callbacks: *const c_void,
    ) -> *mut c_void;
    fn CFDictionarySetValue(dict: *mut c_void, key: *const c_void, value: *const c_void);
    fn CFRelease(cf: *const c_void);

    // Declared as byte arrays with storage: CFDictionaryCreateMutable
    // COPYIES the callback structs by value (~48 bytes each).  A ZST or
    // u64 extern static would make that copy read past the declared
    // object, producing garbage retain/release pointers — the root
    // cause of items silently losing their account attribute.
    static kCFTypeDictionaryKeyCallBacks: [u8; 128];
    static kCFTypeDictionaryValueCallBacks: [u8; 128];
    static kCFBooleanTrue: *const c_void;
}

#[link(name = "Security", kind = "framework")]
extern "C" {
    static kSecClass: *const c_void;
    static kSecClassGenericPassword: *const c_void;
    static kSecAttrService: *const c_void;
    static kSecAttrAccount: *const c_void;
    static kSecValueData: *const c_void;
    static kSecReturnData: *const c_void;
    static kSecAttrAccessible: *const c_void;
    static kSecAttrAccessibleAfterFirstUnlock: *const c_void;

    fn SecItemAdd(query: *const c_void, result: *mut *const c_void) -> i32;
    fn SecItemCopyMatching(query: *const c_void, result: *mut *const c_void) -> i32;
    fn SecItemDelete(query: *const c_void) -> i32;
}

/// Reads the pointer value of an exported `CF*`-reference constant.
///
/// SAFETY: `global` must be the address of an exported framework
/// constant whose storage holds a valid CF reference.
fn sec_global(global: *const *const c_void) -> *const c_void {
    // SAFETY: the constant's storage is always initialized by the
    // framework; we read one pointer.
    unsafe { ptr::read(global) }
}

fn sec_class_generic_password() -> *const c_void {
    sec_global(std::ptr::addr_of!(kSecClassGenericPassword))
}

fn attr_class() -> *const c_void {
    sec_global(std::ptr::addr_of!(kSecClass))
}

fn attr_service() -> *const c_void {
    sec_global(std::ptr::addr_of!(kSecAttrService))
}

fn attr_account() -> *const c_void {
    sec_global(std::ptr::addr_of!(kSecAttrAccount))
}

fn attr_value_data() -> *const c_void {
    sec_global(std::ptr::addr_of!(kSecValueData))
}

fn attr_return_data() -> *const c_void {
    sec_global(std::ptr::addr_of!(kSecReturnData))
}

fn attr_accessible() -> *const c_void {
    sec_global(std::ptr::addr_of!(kSecAttrAccessible))
}

fn attr_accessible_after_first_unlock() -> *const c_void {
    sec_global(std::ptr::addr_of!(kSecAttrAccessibleAfterFirstUnlock))
}

fn cf_boolean_true() -> *const c_void {
    sec_global(std::ptr::addr_of!(kCFBooleanTrue))
}

/// RAII wrapper over a `CFStringRef`.
struct CfString(*const c_void);

impl CfString {
    fn new(value: &str) -> Option<Self> {
        let c = std::ffi::CString::new(value).ok()?;
        // SAFETY: c_str is a valid NUL-terminated buffer for the call;
        // the framework copies it and hands us a +1 reference.
        let cf = unsafe {
            CFStringCreateWithCString(
                ptr::null(),
                c.as_ptr() as *const u8,
                K_CF_STRING_ENCODING_UTF8,
            )
        };
        if cf.is_null() {
            None
        } else {
            Some(Self(cf))
        }
    }

    fn as_ref(&self) -> *const c_void {
        self.0
    }
}

impl Drop for CfString {
    fn drop(&mut self) {
        // SAFETY: valid CFStringRef owned by this wrapper.
        unsafe { CFRelease(self.0) };
    }
}

/// RAII wrapper over a `CFDataRef` returned by the keychain.
pub(crate) struct CfData(*const c_void);

impl CfData {
    /// SAFETY: `cf` must be a `CFDataRef` from a Create/Copy call whose
    /// +1 ownership transfers into this wrapper.
    pub(crate) unsafe fn take(cf: *const c_void) -> Option<Self> {
        if cf.is_null() {
            None
        } else {
            Some(Self(cf))
        }
    }

    pub(crate) fn bytes(&self) -> &[u8] {
        // SAFETY: self.0 is a valid CFDataRef for this wrapper's
        // lifetime.  A zero-length CFData may report a null byte
        // pointer, which must map to an empty slice.
        unsafe {
            let length = CFDataGetLength(self.0) as usize;
            if length == 0 {
                &[]
            } else {
                std::slice::from_raw_parts(CFDataGetBytePtr(self.0), length)
            }
        }
    }
}

impl Drop for CfData {
    fn drop(&mut self) {
        // SAFETY: valid CFDataRef owned by this wrapper.
        unsafe { CFRelease(self.0) };
    }
}

/// RAII wrapper over a `CFMutableDictionaryRef` used as a SecItem query.
struct CfQuery(*mut c_void);

impl CfQuery {
    fn new() -> Option<Self> {
        // SAFETY: framework callback constants are valid; capacity 0
        // means unbounded.
        let dict = unsafe {
            CFDictionaryCreateMutable(
                ptr::null(),
                0,
                std::ptr::addr_of!(kCFTypeDictionaryKeyCallBacks).cast::<c_void>(),
                std::ptr::addr_of!(kCFTypeDictionaryValueCallBacks).cast::<c_void>(),
            )
        };
        if dict.is_null() {
            None
        } else {
            Some(Self(dict))
        }
    }

    fn set(&mut self, key: *const c_void, value: *const c_void) {
        // SAFETY: dict, key and value are valid CF references; the
        // dictionary retains its own copies.
        unsafe { CFDictionarySetValue(self.0, key, value) };
    }

    fn set_string(&mut self, key: *const c_void, value: &CfString) {
        self.set(key, value.as_ref());
    }

    fn set_data(&mut self, key: *const c_void, value: &[u8]) {
        // SAFETY: bytes point at value.len() readable bytes; the
        // framework copies them and we release our +1 below after the
        // dictionary retains its own.
        let data = unsafe { CFDataCreate(ptr::null(), value.as_ptr(), value.len() as isize) };
        if data.is_null() {
            return;
        }
        self.set(key, data);
        // SAFETY: our +1 reference from CFDataCreate; the dictionary
        // retained its own copy in set() above.
        unsafe { CFRelease(data) };
    }

    fn set_true(&mut self, key: *const c_void) {
        self.set(key, cf_boolean_true());
    }

    fn as_query(&self) -> *const c_void {
        self.0
    }
}

/// Builds the standard generic-password query for `service`/`account`.
fn build_query(service: &str, account: &[u8]) -> Option<CfQuery> {
    let service_cf = CfString::new(service)?;
    // macOS 26 finding: kSecAttrAccount must be a CFString here — a
    // CFData account is silently dropped by SecItemAdd/CopyMatching
    // (item created without account; queries match any account).
    // Keys are validated tokens, so lossless UTF-8 is guaranteed.
    let account_str = String::from_utf8_lossy(account).to_string();
    let account_cf = CfString::new(&account_str)?;
    let mut query = CfQuery::new()?;
    // The class constant is a static CFStringRef: set its pointer value
    // directly — it must never be released by us.
    query.set(attr_class(), sec_class_generic_password());
    query.set_string(attr_service(), &service_cf);
    query.set_string(attr_account(), &account_cf);
    Some(query)
}

/// Adds a generic-password item.  Returns the raw Security status.
pub(crate) fn sec_add(service: &str, account: &[u8], value: &[u8]) -> i32 {
    let Some(mut query) = build_query(service, account) else {
        return ERR_SEC_AUTH_FAILED;
    };
    query.set_data(attr_value_data(), value);
    // Available after first unlock: the viewer reads secrets only in an
    // interactive session.
    query.set(attr_accessible(), attr_accessible_after_first_unlock());

    // SAFETY: query is a valid dictionary; the +1 result reference is
    // released immediately (we do not need it).
    let mut result: *const c_void = ptr::null();
    // SAFETY: query is a valid CFDictionary of SecItem search keys.
    let status = unsafe { SecItemAdd(query.as_query(), &mut result) };
    if !result.is_null() {
        // SAFETY: +1 reference returned by SecItemAdd.
        unsafe { CFRelease(result) };
    }
    status
}

/// Copies the generic-password item data.  Returns `(status, data)`.
pub(crate) fn sec_copy(service: &str, account: &[u8]) -> (i32, Option<CfData>) {
    let Some(mut query) = build_query(service, account) else {
        return (ERR_SEC_AUTH_FAILED, None);
    };
    query.set_true(attr_return_data());
    // SAFETY: query valid; a successful call transfers a +1 CFDataRef
    // into CfData::take.
    let mut result: *const c_void = ptr::null();
    // SAFETY: query is a valid CFDictionary of SecItem search keys.
    let status = unsafe { SecItemCopyMatching(query.as_query(), &mut result) };
    let data = if status == ERR_SEC_SUCCESS {
        // SAFETY: +1 reference transferred from the framework.
        unsafe { CfData::take(result) }
    } else {
        None
    };
    (status, data)
}

/// Deletes the generic-password item.  Returns the raw Security status.
pub(crate) fn sec_delete(service: &str, account: &[u8]) -> i32 {
    let Some(query) = build_query(service, account) else {
        return ERR_SEC_AUTH_FAILED;
    };
    // SAFETY: query is a valid dictionary.
    unsafe { SecItemDelete(query.as_query()) }
}

/// Deletes every generic-password item of `service` regardless of
/// account.  Used by tests as a hermetic sweep.
#[cfg(test)]
pub(crate) fn sec_delete_service_all(service: &str) -> i32 {
    let Some(service_cf) = CfString::new(service) else {
        return ERR_SEC_AUTH_FAILED;
    };
    let Some(mut query) = CfQuery::new() else {
        return ERR_SEC_AUTH_FAILED;
    };
    query.set(attr_class(), sec_class_generic_password());
    query.set_string(attr_service(), &service_cf);
    // SAFETY: query is a valid dictionary.
    unsafe { SecItemDelete(query.as_query()) }
}
