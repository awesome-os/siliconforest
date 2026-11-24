# SPDX-License-Identifier: MIT
"""Extract non-static C definition signatures from preprocessed source."""
import re


def definitions(text):
    # Preprocessing has expanded Finch's function-generating macros. Skip string
    # literals when tracking braces so printf formats cannot alter brace depth.
    text = re.sub(r'^\s*#.*$', '', text, flags=re.M)
    tokens = re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{};]', text)
    depth = 0
    start = 0
    header = None
    for token in tokens:
        value = token.group()
        if value.startswith(('"', "'")):
            continue
        if value == '{':
            if depth == 0:
                head = text[start:token.start()].strip()
                # This source uses plain C definitions. Ignore storage objects,
                # typedefs and static helpers, while retaining pointer returns.
                match = re.match(r'([\w\s*<>]+?)\b([A-Za-z_]\w*)\s*\((.*)\)\s*$', head, re.S)
                if match and not re.search(r'\b(static|typedef)\b', match.group(1)):
                    header = (' '.join(match.group(1).split()), match.group(2), ' '.join(match.group(3).split()))
            depth += 1
        elif value == '}':
            depth -= 1
            if depth == 0:
                if header:
                    yield header
                    header = None
                start = token.end()
        elif depth == 0:
            start = token.end()


