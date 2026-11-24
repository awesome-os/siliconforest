"""Read pinned Python as AST data only; never import/execute upstream modules."""
import ast
import hashlib
import re
import subprocess
from pathlib import Path
PIN = '121fcbf35d4501814b58ca5347926a4173b6a512'
INPUTS = {
    'fw/agx/channels.py': 'd1486e64d0b8e60d3272f29cce06a20c564bbf75f89d79b15fb24522132fe60e',
    'agx/channels.py': 'c19ebdaf4dee4867d041d48d39f0b0b0f1689737ecba4800f91097c223008760',
    'fw/agx/initdata.py': 'e9859a76e9397f01aedc43b8627cabd7f80a35f00e34f990673f3e21e8ea99f9',
    'agx/__init__.py': 'c115df7b8ec74424710bda9f6317dc3f1a6e6c729bf801e48707dc5c7edcf199',
    'constructutils.py': '38ee8d703c534975e30459325cff8775ed0bee191640a2c2d365e6d2dc867a4c',
    'hw/uat.py': '4a4d0300cd3a4ed4fb3de250866993b0d1e00ec3249f93077a871e69c94f0838',
}
def fail(message): raise RuntimeError(message)
def require(test, message):
    if not test: fail(message)
def assigned(body, name):
    found = [s.value for s in body if isinstance(s, ast.Assign) and
             any(isinstance(t, ast.Name) and t.id == name for t in s.targets)]
    require(len(found) == 1, 'Not exactly one assignment: ' + name)
    return found[0]
def named_class(tree, name):
    return next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == name)
def audit(root, here):
    base = root / 'sources/m1n1-baseline/proxyclient/m1n1'
    trees, hashes = {}, {}
    for path, expected in INPUTS.items():
        raw = (base / path).read_bytes(); actual = hashlib.sha256(raw).hexdigest()
        require(actual == expected, 'Pinned source changed: ' + path)
        committed = subprocess.run(['git','-C',str(root/'sources/m1n1-baseline'),
            'show',PIN+':proxyclient/m1n1/'+path],capture_output=True,check=True,timeout=20).stdout
        require(hashlib.sha256(committed).hexdigest() == expected, 'Commit object mismatch: '+path)
        hashes[str((base / path).relative_to(root))] = actual
        trees[path] = ast.parse(raw)
    matrix = ast.literal_eval(assigned(named_class(trees['constructutils.py'], 'Ver').body, 'MATRIX'))
    def condition(text):
        result = True
        for part in text.split(' && '):
            m = re.fullmatch(r'([VG]) (==|>=|<) ([A-Z0-9_]+)', part)
            require(m is not None, 'Unsupported version expression')
            key, op, rhs = m.groups(); a = matrix[key].index({'V':'V13_5','G':'G13'}[key]); b = matrix[key].index(rhs)
            result = result and {'==': a == b, '>=': a >= b, '<': a < b}[op]
        return result
    def fields(node, name=None):
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Div):
            return fields(node.right, ast.literal_eval(node.left))
        if isinstance(node, ast.Name):
            sizes = {'Int64ul':8, 'Int32ul':4, 'Int16ul':2}
            require(node.id in sizes, 'Unsupported scalar')
            return [{'name': name, 'size': sizes[node.id], 'endian': 'little'}]
        require(isinstance(node, ast.Call) and isinstance(node.func, ast.Name), 'Unsupported field')
        kind = node.func.id
        if kind == 'Ver': return fields(node.args[1], name) if condition(ast.literal_eval(node.args[0])) else []
        if kind in ('Hex', 'HexDump'): return fields(node.args[0], name)
        if kind == 'Const':
            f = fields(node.args[1], name); f[0]['constant'] = ast.literal_eval(node.args[0]); return f
        if kind == 'Default':
            f = fields(node.args[0], name); default = node.args[1]
            require(isinstance(default, ast.Call) and isinstance(default.func, ast.Name) and default.func.id == 'bytes', 'Nonzero/unknown default')
            require(ast.literal_eval(default.args[0]) == f[0]['size'], 'Default length')
            f[0]['zero_default'] = True; return f
        if kind in ('Bytes', 'ZPadding'):
            return [{'name':name, 'size':ast.literal_eval(node.args[0]), 'opaque_bytes':True}]
        fail('Unsupported construct: ' + kind)
    tree = trees['fw/agx/channels.py']; schemas = {}
    for name in ['DC_Init', 'DC_UpdateIdleTS', 'FWCtlMsg']:
        cls = named_class(tree, name); subcon = assigned(cls.body, 'subcon')
        require(isinstance(subcon, ast.Call) and subcon.func.id == 'Struct', 'Not sequential Struct')
        fs = [f for arg in subcon.args for f in fields(arg)]
        offset = 0
        for f in fs: f['offset'] = offset; offset += f['size']
        schemas[name] = {'line': cls.lineno, 'size': offset, 'fields': fs}
    # Exactly the version branch governing DeviceControlSize.
    branches = [n for n in tree.body if isinstance(n, ast.If) and any(isinstance(s, ast.Assign) and
        any(isinstance(t, ast.Name) and t.id == 'DeviceControlSize' for t in s.targets) for s in n.body)]
    require(len(branches) == 1, 'Size branch count')
    branch = branches[0]
    size = ast.literal_eval(assigned(branch.body if condition(ast.literal_eval(branch.test.args[0])) else branch.orelse, 'DeviceControlSize'))
    require(size == schemas['DC_Init']['size'] == schemas['DC_UpdateIdleTS']['size'] == 48, 'DC size')
    require(schemas['FWCtlMsg']['size'] == 20, 'FWCtl size')
    states = {}
    for name in ['ChannelStateFields', 'FWControlStateFields']:
        cls = named_class(tree, name)
        states[name] = {'line':cls.lineno, 'size':ast.literal_eval(assigned(cls.body, '_SIZE')),
            'read_offset':ast.literal_eval(assigned(cls.body, 'READ_PTR').elts[0]),
            'write_offset':ast.literal_eval(assigned(cls.body, 'WRITE_PTR').elts[0])}
        for field in ['READ_PTR','WRITE_PTR']:
            require(assigned(cls.body,field).elts[1].id == 'Register32', 'Index width')
    require(states['ChannelStateFields']['size']==48 and states['ChannelStateFields']['write_offset']==32, 'Normal state')
    require(states['FWControlStateFields']['size']==32 and states['FWControlStateFields']['write_offset']==16, 'FWCtl state')
    rings = assigned(tree.body, 'channelRings').right.elts
    for item, expected_size in [(rings[0], 48), (rings[-1], 20)]:
        row = item.elts[0].elts
        actual_size = size if isinstance(row[1], ast.Name) else ast.literal_eval(row[1])
        require(actual_size == expected_size and ast.literal_eval(row[2]) == 256, 'Ring size/count')
    cls = named_class(trees['agx/channels.py'], 'GPUFWCtlChannel')
    method = next(n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name == 'send_inval')
    constants = {n.targets[0].attr:ast.literal_eval(n.value) for n in method.body if isinstance(n,ast.Assign) and
        isinstance(n.targets[0],ast.Attribute) and isinstance(n.value,ast.Constant)}
    require(constants == {'unk_8':0,'unk_10':1,'unk_12':2}, 'FWCtl source pattern')
    require(ast.literal_eval(method.args.defaults[0]) == 0, 'FWCtl default address')
    values = {'addr':0x1234564000,'context_id':0xa1b2c3d4,**constants}
    fixtures = {}
    for schema, c_name in [('DC_Init','dc_init'),('DC_UpdateIdleTS','dc_idle'),('FWCtlMsg','fw')]:
        data = bytearray()
        for f in schemas[schema]['fields']:
            if f.get('zero_default'): data.extend(bytes(f['size']))
            else: data.extend((f['constant'] if 'constant' in f else values[f['name']]).to_bytes(f['size'],'little'))
        text = (here / 'test_channel.c').read_text()
        m = re.search(r'uint8_t '+c_name+r'\[\d+\] = \{([^}]+)\}',text)
        require(m is not None, 'Missing C golden')
        literal = bytes(int(v.strip(),0) for v in m[1].split(',') if v.strip())
        literal += bytes(len(data)-len(literal))
        require(literal == data, 'C golden differs from pinned schema: ' + c_name)
        fixtures[c_name] = data.hex()
    return {'MIT_pin':PIN,'input_sha256':hashes,'profile':'Explicit V13_5/G13 pinned source only',
        'loaded_firmware_ABI':'UNKNOWN','sequential_schemas':schemas,'state_schemas':states,'goldens_hex':fixtures,
        'method':'Original restricted AST reader; no upstream imports, Construct execution or downloaded dependencies'}
