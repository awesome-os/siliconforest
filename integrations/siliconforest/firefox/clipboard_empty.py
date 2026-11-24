# SPDX-License-Identifier: MIT
"""Native copy/paste only when the focused Wayland client proves no selection.

No existing clipboard content is replaced. Nonempty/unobserved backends skip
before input; an originally empty selection is conditionally cleared afterward.
Our declarations describe public Objective-C ABI, without platform SDK headers.
"""
import secrets

BRIDGE = r'''
if(!window.__sffxClipboardABI){
 const {ctypes}=ChromeUtils.importESModule("resource://gre/modules/ctypes.sys.mjs");
 const objc=ctypes.open("/usr/lib/libobjc.A.dylib"),system=ctypes.open("/usr/lib/libSystem.B.dylib");
 const ptr=ctypes.voidptr_t,sel=objc.declare("sel_registerName",ctypes.default_abi,ptr,ctypes.char.ptr);
 const cls=objc.declare("objc_getClass",ctypes.default_abi,ptr,ctypes.char.ptr);
 const send=objc.declare("objc_msgSend",ctypes.default_abi,ptr,ptr,ptr);
 const count=objc.declare("objc_msgSend",ctypes.default_abi,ctypes.unsigned_long,ptr,ptr);
 const chars=objc.declare("objc_msgSend",ctypes.default_abi,ctypes.char.ptr,ptr,ptr);
 const sendArg=objc.declare("objc_msgSend",ctypes.default_abi,ptr,ptr,ptr,ptr);
 const make=objc.declare("objc_msgSend",ctypes.default_abi,ptr,ptr,ptr,ctypes.char.ptr);
 const set=objc.declare("objc_msgSend",ctypes.default_abi,ctypes.bool,ptr,ptr,ptr,ptr);
 const dlsym=system.declare("dlsym",ctypes.default_abi,ptr,ptr,ctypes.char.ptr);
 const handle=ctypes.cast(ctypes.intptr_t(-2),ptr);
 const generationPtr=dlsym(handle,"_windowServerSelectionGeneration");
 if(generationPtr.isNull())throw new Error("No observed Wayland clipboard backend");
 const generation=ctypes.cast(generationPtr,ctypes.FunctionType(ctypes.default_abi,ctypes.uint32_t,[]).ptr);
 const board=send(cls("NSPasteboard"),sel("generalPasteboard"));
 const str=s=>make(cls("NSString"),sel("stringWithUTF8String:"),s);
 // A raw ctypes pointer does not retain an autoreleased Objective-C object.
 // This MIME string survives several separate Marionette command pools.
 const type=send(str("public.utf8-plain-text"),sel("retain"));
 window.__sffxClipboardABI={objc,system,
  state(){return {types:Number(count(send(board,sel("types")),sel("count"))),generation:generation()};},
  matches(expected){const value=sendArg(board,sel("stringForType:"),type);
   if(value.isNull())return false;const text=chars(value,sel("UTF8String"));
   return !text.isNull()&&text.readString()===expected;},
  write(text){count(board,sel("clearContents"));return set(board,sel("setString:forType:"),str(text),type);},
  clear(){count(board,sel("clearContents"));return Number(count(send(board,sel("types")),sel("count")))===0;},
  close(){send(type,sel("release"));}
 };
}
const bridge=window.__sffxClipboardABI;
'''


def measure(client, chord):
    result = dict(synthetic_seat_input=True, original_clipboard_data_logged=False,
                  policy='require observed empty backend; preserve foreign changes')
    copy = 'SF-FX-copy-' + secrets.token_hex(8) + ' — δ'
    paste = 'SF-FX-paste-' + secrets.token_hex(8) + ' — λ'

    def chrome(source, args=None):
        client.command('Marionette:SetContext', {'value': 'chrome'})
        return client.command('WebDriver:ExecuteScript', dict(script=BRIDGE + source, args=args or [], newSandbox=False))

    def content(source, args=None):
        client.command('Marionette:SetContext', {'value': 'content'})
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=args or [], newSandbox=False))

    try:
        result['before'] = chrome('return bridge.state();')
    except RuntimeError as error:
        return dict(result, complete=False, skipped=True, reason='Native clipboard bridge unavailable', error=str(error))
    if result['before']['types'] != 0 or result['before']['generation'] == 0:
        return dict(result, complete=False, skipped=True, reason='Existing or unobserved selection preserved')
    try:
        content('const e=document.querySelector("#typing");e.value=arguments[0];e.focus();e.select();return true;', [copy])
        chord(0x06)
        result['copy_matches'] = chrome('return bridge.matches(arguments[0]);', [copy])
        if result['copy_matches']:
            result['write_paste'] = chrome('return bridge.write(arguments[0]);', [paste])
            if result['write_paste']:
                content('const e=document.querySelector("#typing");e.value="";e.focus();return true;')
                chord(0x19)
                result['paste_matches'] = content('return document.querySelector("#typing").value===arguments[0];', [paste])
    finally:
        result['restore'] = chrome('''const own=bridge.matches(arguments[0])||bridge.matches(arguments[1]);
          const state=bridge.state();const result={owned:own,empty:own?bridge.clear():state.types===0,
            foreignChangePreserved:!own&&state.types!==0};
          bridge.close();delete window.__sffxClipboardABI;return result;''', [copy, paste])
    result['complete'] = bool(result.get('copy_matches') and result.get('write_paste') and
        result.get('paste_matches') and result['restore']['empty'] and not result['restore']['foreignChangePreserved'])
    return result
