"""Hash-bound read-only parser dependency; no network or target operations."""
import hashlib
from pathlib import Path
import types
HERE=Path(__file__).resolve().parent
SOURCE=HERE.parents[1]/'m1-network-resolver-egress/revisions/003/host/policy.py'
SOURCE_SHA256='20a0b6d5dbdff17b748f241623fc09989dd83b46957ab68862ae8799fecc6d8b'
raw=SOURCE.read_bytes()
if hashlib.sha256(raw).hexdigest()!=SOURCE_SHA256:raise RuntimeError('frozen resolver003 parser changed')
module=types.ModuleType('frozen_resolver003_packet_parsers')
exec(compile(raw,str(SOURCE),'exec'),module.__dict__)
parse_packet=module.parse_packet
dns_reply_details=module.dns_reply_details
dns_duplicate_comparison=module.dns_duplicate_comparison
http_response=module.http_response
GUEST,DNS,QUESTION=module.GUEST,module.DNS,module.QUESTION
checksum=module.checksum

PacketPolicy=module.Policy
