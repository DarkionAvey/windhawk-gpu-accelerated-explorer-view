"""Read exception/module/stack records without loading the crashed process.

Stack words are address candidates, not an unwound call stack. Uses only the
standard library; dump streams and PE/COFF tables are read as inert bytes.
"""
import bisect
import collections
import pathlib
import struct
import sys


def pe_symbols(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    sections, timestamp, table, count, optional = struct.unpack_from('<HIIIH', data, pe + 6)
    section_start = pe + 24 + optional
    bases = [0] + [struct.unpack_from('<I', data, section_start + i * 40 + 12)[0]
                   for i in range(sections)]
    strings = table + count * 18
    symbols = []
    i = 0
    while table and i < count:
        offset = table + i * 18
        name, value, section, kind, storage, aux = struct.unpack_from('<8sIhHBB', data, offset)
        if name[:4] == b'\0' * 4:
            pos = strings + struct.unpack_from('<I', name, 4)[0]
            name = data[pos:data.find(b'\0', pos)]
        else:
            name = name.split(b'\0')[0]
        if 0 < section < len(bases) and kind & 0x20:
            symbols.append((bases[section] + value, name.decode('utf-8', errors='replace')))
        i += aux + 1
    return timestamp, sorted(symbols)


def inspect(path, dll_directory):
    data = path.read_bytes()
    def unpack(fmt, offset):
        return struct.unpack_from('<' + fmt, data, offset)
    def string(offset):
        size, = unpack('I', offset)
        return data[offset+4:offset+4+size].decode('utf-16-le', errors='replace')
    signature, version, count, directory = unpack('IIII', 0)
    if signature != 0x504d444d:
        raise ValueError('Not a Windows minidump')
    streams = {}
    for i in range(count):
        kind, size, rva = unpack('III', directory + i * 12)
        streams[kind] = (rva, size)
    modules = []
    if 4 in streams:
        start = streams[4][0]
        for i in range(unpack('I', start)[0]):
            base, size, checksum, timestamp, name = unpack('QIIII', start + 4 + i * 108)
            modules.append((base, base + size, string(name), timestamp, False))
    if 14 in streams:
        start = streams[14][0]
        header, entry, count = unpack('III', start)
        for i in range(count):
            base, size, checksum, timestamp, name = unpack('QIIII', start + header + i * entry)
            modules.append((base, base + size, string(name), timestamp, True))
    modules = list(dict.fromkeys(modules))
    symbols = []
    if dll_directory:
        filenames = {pathlib.PureWindowsPath(module[2]).name for module in modules}
        for filename in sorted(filenames):
            dll = dll_directory / filename
            if not dll.is_file():
                continue
            stamp, names = pe_symbols(dll)
            symbols.append((stamp, dll.name, names))
    def locate(address):
        matches = []
        live = [module for module in modules if module[0] <= address < module[1] and not module[4]]
        for begin, end, name, stamp, unloaded in live or modules:
            if begin <= address < end:
                offset = address - begin
                text = f'{pathlib.PureWindowsPath(name).name}+0x{offset:x}'
                if unloaded:
                    text += ' [UNLOADED]'
                for timestamp, filename, names in symbols:
                    if timestamp == stamp and filename.lower() == pathlib.PureWindowsPath(name).name.lower() and names:
                        slot = bisect.bisect_right(names, (offset, chr(0x10ffff))) - 1
                        if slot >= 0:
                            rva, symbol = names[slot]
                            text += f' {filename}!{symbol}+0x{offset-rva:x}'
                matches.append(text)
        return '; '.join(matches)
    start = streams[6][0]
    thread, = unpack('I', start)
    code, flags, record, address, nparams = unpack('IIQQI', start + 8)
    context_size, context = unpack('II', start + 160)
    rsp, rbp = unpack('QQ', context + 152)
    rip, = unpack('Q', context + 248)
    print(f'{path.name}: thread={thread} exception=0x{code:x} address=0x{address:x}')
    print(f'RIP 0x{rip:x} {locate(rip)}; RSP 0x{rsp:x}; RBP 0x{rbp:x}')
    print('Parameters:', ', '.join(hex(v) for v in unpack('Q'*min(nparams,15), start+40)))
    if 3 in streams:
        start = streams[3][0]
        for i in range(unpack('I', start)[0]):
            offset = start + 4 + i * 48
            tid, = unpack('I', offset)
            if tid != thread:
                continue
            base, size, rva = unpack('QII', offset+24)
            first = max(0, rsp-base)
            print('Module-address candidates in the first 1 KiB of the saved stack (not unwound):')
            for delta in range(first, min(size-7, first+1024), 8):
                value, = unpack('Q', rva+delta)
                description = locate(value)
                if description:
                    print(f'  sp+0x{base+delta-rsp:x}: 0x{value:x} {description}')
            if '--histogram' in sys.argv:
                candidates = collections.Counter()
                for delta in range(first, size-7, 8):
                    value, = unpack('Q', rva+delta)
                    description = locate(value)
                    if description:
                        candidates[description] += 1
                print(f'Address-candidate histogram across {size-first} saved stack bytes (not unwound):')
                for description, repeats in candidates.most_common(45):
                    print(f'  {repeats:5d} {description}')


if __name__ == '__main__':
    inspect(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]) if len(sys.argv)>2 else None)
