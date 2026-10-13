from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]

def block(source, marker):
    start = source.index(marker)
    brace = source.index('{', start)
    while source.find(';', start, brace) != -1:
        start = source.index(marker, start + len(marker))
        brace = source.index('{', start)
    level, end = 1, brace + 1
    while level:
        level += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

def without_diagnostic_instrumentation(source):
    # CPU doubles omit only the explicitly marked diagnostic callbacks.
    for name in ['DISPATCHTRACE', 'POOLTRACE']:
        source = re.sub(r'^[ \t]*// ' + name + r' BEGIN\n.*?^[ \t]*// ' + name + r' END\n',
                        '', source, flags=re.M | re.S)
    return '\n'.join(line for line in source.split('\n') if '// POOLTRACE' not in line)
