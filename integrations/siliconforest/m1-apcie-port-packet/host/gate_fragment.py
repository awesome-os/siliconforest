# Original isolated opt-in. No hardware operation; consumed by prepare.py.
APCIE_NATIVE_MEMBERS = ()
APCIE_OBSERVER_MEMBER = 'org.puredarwin.driver.PDApcieT8103Bridge'

def apcie_port_observer_enabled(inputs):
    record = json.loads(inputs.contents['build_record'], object_pairs_hook=image.unique)
    image.require(type(record) is dict, 'APCIE observer requires an object record')
    members = inputs.report['kernel']['members']
    if 'apcie_port_observer_experiment' not in record:
        image.require(APCIE_OBSERVER_MEMBER not in members, 'APCIE member without opt-in')
        return False
    experiment = record['apcie_port_observer_experiment']
    expected = {'version': 1, 'observer': 'PDApcieT8103Bridge', 'snapshot_bytes': 84,
                'read_only': True, 'shared_writes': False}
    image.require(type(experiment) is dict and type(experiment.get('version')) is int and
                  type(experiment.get('snapshot_bytes')) is int and
                  type(experiment.get('read_only')) is bool and
                  type(experiment.get('shared_writes')) is bool and experiment == expected,
                  'Unknown APCIE read-only observer opt-in')
    allowed = {'desktop_experiment', 'native_trackpad_child_experiment', 'apcie_port_observer_experiment'}
    image.require(all(type(k) is str and
                  (not k.endswith('_experiment') or k in allowed) and
                  not k.startswith(('network_', 'terminal_', 'virtual_', 'keyboard_',
                                    'dart_', 'survey_', 'trackpad_enumeration_'))
                  for k in record), 'Conflicting APCIE experiment fields')
    image.require(native_trackpad_child_enabled(inputs), 'APCIE observer requires sealed native child')
    expected_members = set(APCIE_NATIVE_MEMBERS) | {APCIE_OBSERVER_MEMBER}
    image.require(type(members) is dict and len(members) == 51 and set(members) == expected_members,
                  'APCIE observer requires exact native50 plus observer members')
    raw = inputs.contents['kernel']
    kind, _, commands = image.commands(raw)
    image.require(kind == 12, 'APCIE observer requires native fileset')
    actual = []
    for cmd, off, size in commands:
        if cmd != 0x80000035:
            continue
        image.require(size >= 32, 'APCIE truncated fileset entry')
        nameoff = struct.unpack_from('<I', raw, off + 24)[0]
        image.require(32 <= nameoff < size and b'\0' in raw[off + nameoff:off + size],
                      'APCIE invalid fileset name')
        try:
            actual.append(raw[off + nameoff:off + size].split(b'\0', 1)[0].decode('ascii'))
        except UnicodeDecodeError as error:
            raise ValueError('APCIE non-ASCII fileset name') from error
    image.require(len(actual) == 51 and len(set(actual)) == 51 and set(actual) == expected_members,
                  'APCIE raw fileset must contain exactly native50 plus one observer')
    return True


def apcie_port_runtime(inputs, runtime, timeout):
    if apcie_port_observer_enabled(inputs):
        image.require(type(runtime) is int and runtime == 120 and
                      type(timeout) is int and timeout == 240,
                      'APCIE observer requires runtime120 and worker240')
