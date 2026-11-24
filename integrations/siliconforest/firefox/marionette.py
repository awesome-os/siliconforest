#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Small local Marionette client for measurements in the official Firefox binary.

The port is bound by Firefox on target loopback. Browser commands are explicit;
no mock page state is used. Diagnostic profiles opt into system-context access.
"""
import json
import socket
import time


class Marionette:
    def __init__(self, port=28290, timeout=10):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=timeout)
        self.sequence = 0
        self.hello = self.receive()

    def receive(self):
        header = bytearray()
        while True:
            byte = self.socket.recv(1)
            if not byte:
                raise EOFError('Marionette closed the connection')
            if byte == b':':
                break
            header.extend(byte)
            if len(header) > 12:
                raise ValueError('Invalid Marionette frame length')
        length = int(header)
        if length > 64 * 1024 * 1024:
            raise ValueError('Oversized Marionette reply')
        payload = bytearray()
        while len(payload) < length:
            byte = self.socket.recv(length - len(payload))
            if not byte:
                raise EOFError('Truncated Marionette reply')
            payload.extend(byte)
        return json.loads(payload)

    def command(self, name, parameters=None):
        self.sequence += 1
        payload = json.dumps([0, self.sequence, name, parameters or {}]).encode()
        self.socket.sendall(str(len(payload)).encode() + b':' + payload)
        response = self.receive()
        if response[:2] != [1, self.sequence]:
            raise RuntimeError('Unexpected Marionette response: ' + repr(response))
        if response[2]:
            raise RuntimeError(response[2])
        result = response[3]
        return result['value'] if isinstance(result, dict) and set(result) == {'value'} else result

    def start(self, page_load_strategy='normal'):
        # Marionette takes capabilities directly. The HTTP WebDriver envelope
        # is handled by geckodriver, not by this local wire protocol.
        result = self.command('WebDriver:NewSession', {
            'pageLoadStrategy': page_load_strategy})
        if result['capabilities']['pageLoadStrategy'] != page_load_strategy:
            raise RuntimeError('Firefox did not accept the requested page-load strategy')
        return result

    def support(self):
        self.command('Marionette:SetContext', {'value': 'chrome'})
        return self.command('WebDriver:ExecuteAsyncScript', {
            'script': '''const done = arguments[arguments.length - 1];
            ChromeUtils.importESModule("resource://gre/modules/Troubleshoot.sys.mjs")
              .Troubleshoot.snapshot().then(done, e => done({error: String(e)}));''',
            'args': [], 'scriptTimeout': 15000, 'newSandbox': True,
        })

    def runtime(self):
        self.command('Marionette:SetContext', {'value': 'chrome'})
        return self.command('WebDriver:ExecuteScript', {
            'script': '''const result={preferences:{},startup:{}};
            try {
              const prefs=Components.classes["@mozilla.org/preferences-service;1"]
                .getService(Components.interfaces.nsIPrefBranch);
              for(const name of ["javascript.options.baselinejit","javascript.options.ion",
                "javascript.options.wasm_baselinejit","javascript.options.wasm_optimizingjit"]){
                try{result.preferences[name]=prefs.getBoolPref(name);}
                catch(error){result.preferences[name]={error:String(error)};}
              }
              const info=Components.classes["@mozilla.org/toolkit/app-startup;1"]
                .getService(Components.interfaces.nsIAppStartup).getStartupInfo();
              for(const [name,value] of Object.entries(info))
                result.startup[name]=value instanceof Date?value.getTime():String(value);
            } catch(error){result.error=String(error);}
            return result;''', 'args': [], 'newSandbox': True,
        })

    def measure_fixture(self, scroll=False, duration=10000):
        if not 1000 <= duration <= 60000:
            raise ValueError('Fixture duration must be1000..60000ms')
        self.command('WebDriver:SetTimeouts', {'script': duration+4000})
        self.command('Marionette:SetContext', {'value': 'content'})
        handles = self.command('WebDriver:GetWindowHandles')
        for handle in handles:
            self.command('WebDriver:SwitchToWindow', {'handle': handle})
            found = self.command('WebDriver:ExecuteScript', {
                'script': 'return typeof window.fxMeasureFrames === "function";',
                'args': [], 'newSandbox': False,
            })
            if found:
                return self.command('WebDriver:ExecuteAsyncScript', {
                    'script': 'const done=arguments[arguments.length-1]; window.fxMeasureFrames(arguments[1],arguments[0]).then(done);',
                    'args': [scroll,duration], 'scriptTimeout': duration+4000, 'newSandbox': False,
                })
        raise RuntimeError('No owned measurement fixture found')

    def video_handles(self):
        self.command('Marionette:SetContext', {'value': 'content'})
        result = []
        for handle in self.command('WebDriver:GetWindowHandles'):
            self.command('WebDriver:SwitchToWindow', {'handle': handle})
            found = self.command('WebDriver:ExecuteScript', {
                'script': 'return typeof window.fxMeasureVideo === "function";',
                'args': [], 'newSandbox': False,
            })
            if found:
                result.append(handle)
        return result

    def measure_video(self, handle=None):
        self.command('WebDriver:SetTimeouts', {'script': 74000})
        if handle is None:
            handles = self.video_handles()
            if not handles:
                raise RuntimeError('No owned video fixture found')
            handle = handles[0]
        self.command('WebDriver:SwitchToWindow', {'handle': handle})
        return self.command('WebDriver:ExecuteAsyncScript', {
            'script': 'const done=arguments[arguments.length-1]; window.fxMeasureVideo().then(done);',
            'args': [], 'scriptTimeout': 74000, 'newSandbox': False,
        })

    def pages(self, capture=None):
        self.command('Marionette:SetContext', {'value': 'content'})
        handles = self.command('WebDriver:GetWindowHandles')
        if isinstance(handles, dict):
            handles = handles['value']
        result = []
        for handle in handles:
            self.command('WebDriver:SwitchToWindow', {'handle': handle})
            page = self.command('WebDriver:ExecuteScript', {
                'script': "return {url:location.href,title:document.title,body:document.body.innerText.slice(0,4000),fixture:window.fxResult||null,video:window.fxVideoResult||null};",
                'args': [], 'newSandbox': False,
            })
            result.append(page)
            if capture:
                time.sleep(.25)
                capture(len(result)-1, page['url'])
        return result

    def close(self):
        self.socket.close()
