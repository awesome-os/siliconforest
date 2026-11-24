"""Pure saved-block decoder. Never imports a transport or touches a device."""
import hashlib
import re
import struct


def decode(block, nonce):
    if not isinstance(nonce, str) or re.fullmatch('[0-9a-f]{48}', nonce) is None:
        raise ValueError('nonce')
    # UART CR insertion is framing; never transform the represented raw bytes.
    lines = block.replace(b'\r', b'').decode('ascii').splitlines()
    if len(lines) != 3 or lines[0] != f'DL25_GPU_PREFLIGHT_BEGIN_{nonce} bytes=64':
        raise ValueError('block start/length')
    hexline = re.fullmatch(f'DL25_GPU_PREFLIGHT_HEX_{nonce} index=0 data=([0-9a-f]{{128}})', lines[1])
    end = re.fullmatch(f'DL25_GPU_PREFLIGHT_END_{nonce} sha256=([0-9a-f]{{64}})', lines[2])
    if hexline is None or end is None:
        raise ValueError('hex/index/end')
    raw = bytes.fromhex(hexline[1])
    if hashlib.sha256(raw).hexdigest() != end[1]:
        raise ValueError('digest')
    w = struct.unpack('<16I', raw)
    version, size, result, board, attempted, completed, power, reserved = w[:8]
    if version != 1 or size != 64 or result not in (0, 1, 2) or reserved != 0 or w[14:] != (0, 0):
        raise ValueError('version/profile/no-GPU-read contract')
    if result == 1:
        if w[3:] != (0,) * 13:
            raise ValueError('board refusal shape')
        return dict(status='BOARD_REFUSED', raw=raw, gpu_mmio_authorized=False)
    if board != 1 or attempted != 1 or w[8:14] != (314, 315, 316, 7, 99, 0x3f8):
        raise ValueError('bound GPU/PMGR profile')
    if result == 2:
        if completed or power:
            raise ValueError('read failure shape')
        return dict(status='PMGR_READ_FAILED', raw=raw, gpu_mmio_authorized=False)
    if completed != 1:
        raise ValueError('missing actual read')
    return dict(status='PMGR_OBSERVED_GPU_ACCESS_NOT_QUALIFIED', raw=raw,
                gfx_power=power, target=power & 15, actual=(power >> 4) & 15,
                gpu_mmio_authorized=False)
