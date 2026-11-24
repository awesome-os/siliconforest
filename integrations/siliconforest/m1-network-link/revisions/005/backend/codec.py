"""Original host binding: C owns its state; Python never declares its layout."""
import ctypes as C
import re


class Codec:
    def __init__(self, library, nonce, direction):
        if not re.fullmatch(r"[0-9a-f]{48}", nonce):
            raise ValueError("exact lowerhex48 nonce required")
        self.lib = C.CDLL(str(library))
        self.callback_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint8,
                                        C.c_uint64, C.c_void_p, C.c_size_t)
        self.lib.dln_host_create.argtypes = [C.c_char_p, C.c_size_t, C.c_uint8]
        self.lib.dln_host_create.restype = C.c_void_p
        self.lib.dln_host_destroy.argtypes = [C.c_void_p]
        self.lib.dln_host_destroy.restype = None
        self.lib.dln_host_encode.argtypes = [C.c_void_p, C.c_uint8, C.c_void_p,
                                            C.c_size_t, C.c_void_p, C.c_size_t,
                                            C.POINTER(C.c_size_t)]
        self.lib.dln_host_encode.restype = C.c_int
        self.lib.dln_host_feed.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t,
                                          self.callback_type, C.c_void_p]
        self.lib.dln_host_feed.restype = C.c_int
        self.lib.dln_host_end.argtypes = [C.c_void_p]
        self.lib.dln_host_end.restype = C.c_int
        self.lib.dln_error_name.argtypes = [C.c_int]
        self.lib.dln_error_name.restype = C.c_char_p
        self.context = self.lib.dln_host_create(nonce.encode(), 48, direction)
        if not self.context:
            raise RuntimeError("codec allocation/init failed")

    def check(self, status):
        if status:
            raise ValueError("DLNET001 " + self.lib.dln_error_name(status).decode("ascii"))

    def encode(self, kind, packet=b""):
        out, written = C.create_string_buffer(1556), C.c_size_t()
        self.check(self.lib.dln_host_encode(self.context, kind, packet, len(packet),
                                            out, len(out), C.byref(written)))
        return out.raw[:written.value]

    def feed(self, data, receive):
        errors = []
        def invoke(_context, kind, seq, pointer, n):
            try:
                receive(kind, seq, C.string_at(pointer, n))
                return 0
            except BaseException as error:
                errors.append(error)
                return 1
        callback = self.callback_type(invoke)
        status = self.lib.dln_host_feed(self.context, data, len(data), callback, None)
        if errors:
            raise errors[0]
        self.check(status)

    def end(self):
        self.check(self.lib.dln_host_end(self.context))

    def close(self):
        if self.context:
            self.lib.dln_host_destroy(self.context)
            self.context = None
