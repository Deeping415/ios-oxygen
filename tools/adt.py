# Minimal Apple Device Tree (ADT) parser / serializer for Python 3.
#
# Node layout:
#   props_count  u32
#   child_count  u32
#   props[props_count]:
#     name       char[32] (null-padded)
#     flags      u32      (bit 31 = placeholder / internal)
#     size       u32
#     val        char[round_up(size, 4)]
#   children[child_count]:
#     Node...

import struct

class Node:
    def __init__(self, name=""):
        self.props = []     # list of (name: str, flags: int, val: bytes)
        self.children = []  # list of Node

    @property
    def name(self):
        for k, flags, val in self.props:
            if k == "name":
                return val.rstrip(b"\0").decode("latin1", errors="replace")
        return ""

    def prop(self, k):
        for name, flags, val in self.props:
            if name == k:
                return val
        return None

def parse(data, off=0):
    nprops, nchildren = struct.unpack_from("<II", data, off)
    off += 8
    node = Node()
    for _ in range(nprops):
        pname_raw, pflags, psize = struct.unpack_from("<32sII", data, off)
        off += 40
        pname = pname_raw.split(b"\0", 1)[0].decode("latin1")
        val = data[off : off + psize]
        off += (psize + 3) & ~3
        node.props.append((pname, pflags, val))
    for _ in range(nchildren):
        child, off = parse(data, off)
        node.children.append(child)
    return node, off

def serialize(node):
    nprops = len(node.props)
    nchildren = len(node.children)
    out = bytearray(struct.pack("<II", nprops, nchildren))
    for name, flags, val in node.props:
        name_bytes = name.encode("latin1")[:31] + b"\0"
        name_pad = name_bytes + b"\0" * (32 - len(name_bytes))
        size = len(val)
        out += struct.pack("<32sII", name_pad, flags, size)
        out += val
        out += b"\0" * (-len(val) & 3)
    for child in node.children:
        out += serialize(child)
    return bytes(out)
