#!/usr/bin/env python3

# Copyright (c) 2021, Timothy Stack
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
# * Neither the name of Timothy Stack nor the names of its contributors
# may be used to endorse or promote products derived from this software
# without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import sys
import glob
import struct
import hashlib
import select
import stat
import enum
from typing import List, Optional


class TailerPacketType(enum.IntEnum):
    TPT_ERROR = 0
    TPT_OPEN_PATH = 1
    TPT_CLOSE_PATH = 2
    TPT_OFFER_BLOCK = 3
    TPT_NEED_BLOCK = 4
    TPT_ACK_BLOCK = 5
    TPT_TAIL_BLOCK = 6
    TPT_LINK_BLOCK = 7
    TPT_SYNCED = 8
    TPT_LOG = 9
    TPT_LOAD_PREVIEW = 10
    TPT_PREVIEW_ERROR = 11
    TPT_PREVIEW_DATA = 12
    TPT_COMPLETE_PATH = 13
    TPT_POSSIBLE_PATH = 14
    TPT_ANNOUNCE = 15


class TailerPacketPayloadType(enum.IntEnum):
    TPPT_DONE = 0
    TPPT_STRING = 1
    TPPT_HASH = 2
    TPPT_INT64 = 3
    TPPT_BITS = 4


class ClientState(enum.IntEnum):
    CS_INIT = 0
    CS_OFFERED = 1
    CS_TAILING = 2
    CS_SYNCED = 3


class PathState(enum.IntEnum):
    PS_UNKNOWN = 0
    PS_OK = 1
    PS_ERROR = 2


class RecvState(enum.IntEnum):
    RS_ERROR = 0
    RS_PACKET_TYPE = 1
    RS_PAYLOAD_TYPE = 2
    RS_PAYLOAD = 3
    RS_PAYLOAD_LENGTH = 4
    RS_PAYLOAD_CONTENT = 5


SHA256_BLOCK_SIZE = 32


def is_glob(fn: str) -> bool:
    return any(c in fn for c in '*?[')


def send_packet(fd: int, packet_type: int, *args):
    """
    Serializes and writes a packet to the given file descriptor.
    Format inferred from C read functions:
    [PacketType(int32)]
    Then loop:
      [PayloadType(int32)]
      If String/Bits: [Length(int32)] [Bytes]
      If Int64: [Value(int64)]
      If Hash: [Bytes(32)]
    End with [TPPT_DONE(int32)]
    """
    data = bytearray()

    # Packet Type (assuming int32 based on C enum size usually, could be different)
    data.extend(struct.pack('i', int(packet_type)))

    for arg in args:
        if len(arg) < 2:
            continue

        ptype = arg[0]
        value = arg[1]

        data.extend(struct.pack('i', int(ptype)))

        if ptype == TailerPacketPayloadType.TPPT_STRING:
            if isinstance(value, str):
                b_value = value.encode('utf-8')
            else:
                b_value = value
            data.extend(struct.pack('i', len(b_value)))
            data.extend(b_value)

        elif ptype == TailerPacketPayloadType.TPPT_BITS:
            # args expected: (TPPT_BITS, bytes)
            # Python generic args handling: value is length, arg[2] is content
            data.extend(struct.pack('i', len(value)))
            data.extend(value)

        elif ptype == TailerPacketPayloadType.TPPT_INT64:
            data.extend(struct.pack('q', value))

        elif ptype == TailerPacketPayloadType.TPPT_HASH:
            # value expected to be bytes
            if len(value) != SHA256_BLOCK_SIZE:
                # Padding or truncation if necessary, though strictly shouldn't happen
                value = value.ljust(SHA256_BLOCK_SIZE, b'\0')[:SHA256_BLOCK_SIZE]
            data.extend(value)

    # Terminator
    data.extend(struct.pack('i', int(TailerPacketPayloadType.TPPT_DONE)))

    try:
        os.write(fd, data)
    except OSError:
        pass


class ClientPathState:
    def __init__(self, path: str):
        self.cps_path = path
        self.cps_last_path_state = PathState.PS_UNKNOWN
        self.cps_last_stat: Optional[os.stat_result] = None
        self.cps_client_file_offset = -1
        self.cps_client_file_size = 0
        self.cps_client_state = ClientState.CS_INIT
        self.cps_children: List['ClientPathState'] = []

    def find_child(self, path: str) -> Optional['ClientPathState']:
        for child in self.cps_children:
            if child.cps_path == path:
                return child
        return None


class Tailer:
    def __init__(self):
        self.client_path_list: List[ClientPathState] = []
        self.running = True

    def send_error(self, cps: ClientPathState, msg: str):
        send_packet(sys.stdout.fileno(),
                    TailerPacketType.TPT_ERROR,
                    (TailerPacketPayloadType.TPPT_STRING, cps.cps_path),
                    (TailerPacketPayloadType.TPPT_STRING, msg))

    def set_client_path_state_error(self, cps: ClientPathState, op: str, error_msg: Optional[str]):
        if cps.cps_last_path_state != PathState.PS_ERROR:
            self.send_error(cps, f"unable to {op} -- {error_msg}")

        cps.cps_last_path_state = PathState.PS_ERROR
        cps.cps_client_file_offset = -1
        cps.cps_client_state = ClientState.CS_INIT
        cps.cps_children.clear()

    def send_preview_error(self, _id: int, path: str, msg: str):
        send_packet(sys.stdout.fileno(),
                    TailerPacketType.TPT_PREVIEW_ERROR,
                    (TailerPacketPayloadType.TPPT_INT64, _id),
                    (TailerPacketPayloadType.TPPT_STRING, path),
                    (TailerPacketPayloadType.TPPT_STRING, msg))

    def send_preview_data(self, _id: int, path: str, bits: bytes):
        send_packet(sys.stdout.fileno(),
                    TailerPacketType.TPT_PREVIEW_DATA,
                    (TailerPacketPayloadType.TPPT_INT64, _id),
                    (TailerPacketPayloadType.TPPT_STRING, path),
                    (TailerPacketPayloadType.TPPT_BITS, bits))

    def find_client_path_state(self, path_list: List[ClientPathState], path: str) -> Optional[ClientPathState]:
        for curr in path_list:
            if curr.cps_path == path:
                return curr

            child = self.find_client_path_state(curr.cps_children, path)
            if child:
                return child
        return None

    def create_client_path_state(self, path: str) -> ClientPathState:
        return ClientPathState(path)

    def poll_paths(self, path_list: List[ClientPathState],
                   root_cps: Optional[ClientPathState] = None) -> int:
        retval = 0

        # Iterate over a copy/index to allow modification safely if logic requires, 
        # though C logic modifies linked list structure.
        # In C: iterates list, possibly replacing children.

        for curr in path_list:
            if root_cps is None:
                current_root = curr
            else:
                current_root = root_cps

            if is_glob(curr.cps_path):
                changes = 0
                try:
                    # Glob expansion
                    matches = glob.glob(curr.cps_path)

                    prev_children = curr.cps_children[:]
                    curr.cps_children = []

                    for match in matches:
                        child = next((c for c in prev_children if c.cps_path == match), None)
                        if child is None:
                            child = self.create_client_path_state(match)
                            changes += 1
                        else:
                            prev_children.remove(child)
                        curr.cps_children.append(child)

                    # Deleted children
                    for child in prev_children:
                        self.send_error(child, "deleted")
                        changes += 1

                    retval += self.poll_paths(curr.cps_children, current_root)

                except Exception as e:
                    self.set_client_path_state_error(curr, "glob", str(e))

                if changes:
                    curr.cps_client_state = ClientState.CS_INIT
                elif curr.cps_client_state != ClientState.CS_SYNCED:
                    send_packet(sys.stdout.fileno(),
                                TailerPacketType.TPT_SYNCED,
                                (TailerPacketPayloadType.TPPT_STRING, current_root.cps_path),
                                (TailerPacketPayloadType.TPPT_STRING, curr.cps_path))
                    curr.cps_client_state = ClientState.CS_SYNCED

                continue

            # Stat logic
            try:
                st = os.lstat(curr.cps_path)
            except OSError as e:
                st = None
                self.set_client_path_state_error(curr, "lstat", e.strerror)

            if st:
                # Check for replacement
                if (curr.cps_client_file_offset >= 0 and curr.cps_last_stat and
                        (curr.cps_last_stat.st_dev != st.st_dev or
                         curr.cps_last_stat.st_ino != st.st_ino or
                         st.st_size < curr.cps_last_stat.st_size)):
                    self.send_error(curr, "replaced")
                    self.set_client_path_state_error(curr, "replace", "file replaced")
                    # We continue processing but state is now error/reset

                elif stat.S_ISLNK(st.st_mode):
                    if curr.cps_client_state == ClientState.CS_INIT:
                        try:
                            target = os.readlink(curr.cps_path)
                            send_packet(sys.stdout.fileno(),
                                        TailerPacketType.TPT_LINK_BLOCK,
                                        (TailerPacketPayloadType.TPPT_STRING, current_root.cps_path),
                                        (TailerPacketPayloadType.TPPT_STRING, curr.cps_path),
                                        (TailerPacketPayloadType.TPPT_STRING, target))
                            curr.cps_client_state = ClientState.CS_SYNCED

                            if target.startswith('/'):
                                child = self.create_client_path_state(target)
                                sys.stderr.write(f"info: monitoring link path {target}\n")
                                curr.cps_children.append(child)

                            retval += 1
                        except OSError as e:
                            self.set_client_path_state_error(curr, "readlink",
                                                             e.strerror)

                    retval += self.poll_paths(curr.cps_children, current_root)
                    curr.cps_last_path_state = PathState.PS_OK

                elif stat.S_ISREG(st.st_mode):
                    if curr.cps_client_state in (ClientState.CS_INIT,
                                                 ClientState.CS_TAILING,
                                                 ClientState.CS_SYNCED):
                        if curr.cps_client_file_offset < st.st_size:
                            try:
                                fd = os.open(curr.cps_path, os.O_RDONLY)
                                try:
                                    file_offset = max(0,
                                                      curr.cps_client_file_offset)

                                    # Determine read size
                                    nbytes = 4 * 1024 * 1024
                                    if curr.cps_client_state == ClientState.CS_INIT:
                                        if curr.cps_client_file_size == 0:
                                            nbytes = 32 * 1024
                                        elif file_offset < curr.cps_client_file_size:
                                            nbytes = curr.cps_client_file_size - file_offset
                                            if nbytes > 4 * 1024 * 1024:
                                                nbytes = 4 * 1024 * 1024

                                    # Pread
                                    # Python's os.pread handles the offset logic
                                    buffer = os.pread(fd, nbytes, file_offset)
                                    bytes_read = len(buffer)

                                    if curr.cps_client_state == ClientState.CS_INIT and (
                                            curr.cps_client_file_offset < 0 or bytes_read > 0):
                                        # Offer block logic (Hashing)
                                        remaining = 0
                                        remaining_offset = file_offset + bytes_read

                                        if curr.cps_client_file_size > 0 and file_offset < curr.cps_client_file_size:
                                            remaining = curr.cps_client_file_size - file_offset - bytes_read

                                        sys.stderr.write(
                                            f"info: prepping offer: init={bytes_read}; remaining={remaining}; {curr.cps_path}\n")

                                        sha = hashlib.sha256()
                                        sha.update(buffer)

                                        # Hash remaining if necessary
                                        temp_hash_buffer_size = 4 * 1024 * 1024
                                        while remaining > 0:
                                            read_len = min(remaining,
                                                           temp_hash_buffer_size)
                                            chunk = os.pread(fd, read_len,
                                                             remaining_offset)
                                            if not chunk:
                                                remaining = 0
                                                break
                                            sha.update(chunk)
                                            remaining -= len(chunk)
                                            remaining_offset += len(chunk)
                                            bytes_read += len(chunk)

                                        if remaining == 0:
                                            digest = sha.digest()
                                            send_packet(sys.stdout.fileno(),
                                                        TailerPacketType.TPT_OFFER_BLOCK,
                                                        (TailerPacketPayloadType.TPPT_STRING,
                                                         current_root.cps_path),
                                                        (TailerPacketPayloadType.TPPT_STRING, curr.cps_path),
                                                        (TailerPacketPayloadType.TPPT_INT64, int(st.st_mtime)),
                                                        (TailerPacketPayloadType.TPPT_INT64, file_offset),
                                                        (TailerPacketPayloadType.TPPT_INT64, int(bytes_read)),
                                                        (TailerPacketPayloadType.TPPT_HASH, digest))
                                            curr.cps_client_state = ClientState.CS_OFFERED

                                    else:
                                        # Tailing
                                        if curr.cps_client_file_offset < 0:
                                            curr.cps_client_file_offset = 0

                                        send_packet(sys.stdout.fileno(),
                                                    TailerPacketType.TPT_TAIL_BLOCK,
                                                    (TailerPacketPayloadType.TPPT_STRING, current_root.cps_path),
                                                    (TailerPacketPayloadType.TPPT_STRING, curr.cps_path),
                                                    (TailerPacketPayloadType.TPPT_INT64, int(st.st_mtime)),
                                                    (TailerPacketPayloadType.TPPT_INT64, curr.cps_client_file_offset),
                                                    (TailerPacketPayloadType.TPPT_BITS, buffer))

                                        curr.cps_client_file_offset += bytes_read
                                        curr.cps_client_state = ClientState.CS_TAILING

                                    retval = 1
                                finally:
                                    os.close(fd)
                            except OSError as e:
                                self.set_client_path_state_error(curr, "open/read", e.strerror)

                        elif curr.cps_client_state != ClientState.CS_SYNCED:
                            send_packet(sys.stdout.fileno(),
                                        TailerPacketType.TPT_SYNCED,
                                        (TailerPacketPayloadType.TPPT_STRING, current_root.cps_path),
                                        (TailerPacketPayloadType.TPPT_STRING, curr.cps_path))
                            curr.cps_client_state = ClientState.CS_SYNCED

                    # Case CS_OFFERED: waiting for ack, do nothing
                    curr.cps_last_path_state = PathState.PS_OK

                elif stat.S_ISDIR(st.st_mode):
                    try:
                        entries = os.listdir(curr.cps_path)
                        prev_children = curr.cps_children[:]
                        curr.cps_children = []
                        changes = 0

                        for entry in entries:
                            if entry in ('.', '..'):
                                continue

                            full_path = os.path.join(curr.cps_path, entry)

                            # Check type of child (simplified compared to C which checks d_type)
                            # We defer detailed type check to the recursive call, but C checks DT_REG/DT_LNK here.
                            # lstat is expensive, but necessary if d_type unavailable.
                            try:
                                child_st = os.lstat(full_path)
                                if not (stat.S_ISREG(child_st.st_mode) or stat.S_ISLNK(child_st.st_mode)):
                                    continue
                            except OSError:
                                continue

                            child = next((c for c in prev_children if c.cps_path == full_path), None)
                            if child is None:
                                sys.stderr.write(
                                    f"info: monitoring child path: {full_path}\n")
                                child = self.create_client_path_state(full_path)
                                changes += 1
                            else:
                                prev_children.remove(child)

                            curr.cps_children.append(child)

                        for child in prev_children:
                            self.send_error(child, "deleted")
                            changes += 1

                        retval += self.poll_paths(curr.cps_children, current_root)

                        if changes:
                            curr.cps_client_state = ClientState.CS_INIT
                        elif curr.cps_client_state != ClientState.CS_SYNCED:
                            send_packet(sys.stdout.fileno(),
                                        TailerPacketType.TPT_SYNCED,
                                        (TailerPacketPayloadType.TPPT_STRING, current_root.cps_path),
                                        (TailerPacketPayloadType.TPPT_STRING, curr.cps_path))
                            curr.cps_client_state = ClientState.CS_SYNCED

                    except OSError as e:
                        self.set_client_path_state_error(curr, "opendir", e.strerror)

                    curr.cps_last_path_state = PathState.PS_OK

                curr.cps_last_stat = st

        sys.stderr.flush()
        return retval

    def send_possible_paths(self, glob_path: str, depth: int):
        try:
            for child_path in glob.glob(glob_path):
                send_packet(sys.stdout.fileno(),
                            TailerPacketType.TPT_POSSIBLE_PATH,
                            (TailerPacketPayloadType.TPPT_STRING, child_path))

                if depth == 0 and os.path.isdir(child_path):
                    self.send_possible_paths(child_path + "/*", depth + 1)
        except Exception:
            pass

    def handle_load_preview_request(self, path: str, preview_id: int):
        sys.stderr.write(f"info: load preview request -- {preview_id}\n")

        if is_glob(path):
            try:
                bits = []
                count = 0
                for match in glob.glob(path):
                    if count >= 10:
                        bits.append(" ... and more! ...\n")
                        break
                    bits.append(match + "\n")
                    count += 1

                content = "".join(bits).encode('utf-8')
                self.send_preview_data(preview_id, path, content)
            except Exception as e:
                self.send_preview_error(preview_id, path,
                                        f"error: glob failed -- {e}")

        else:
            try:
                st = os.stat(path)
                if stat.S_ISREG(st.st_mode):
                    try:
                        with open(path, 'rb') as f:
                            # Read up to 10 lines or capacity limit
                            content = bytearray()
                            lines_read = 0
                            capacity = 1024 * 1024

                            while lines_read < 10 and len(content) < capacity:
                                line = f.readline(capacity - len(content))
                                if not line:
                                    break
                                content.extend(line)
                                lines_read += 1

                            self.send_preview_data(preview_id, path, content)
                    except OSError as e:
                        self.send_preview_error(preview_id, path, f"error: cannot open {path} -- {e.strerror}")

                elif stat.S_ISDIR(st.st_mode):
                    try:
                        entries = os.listdir(path)
                        bits = []
                        count = 0
                        for entry in entries:
                            # Simple filter
                            if entry in ('.', '..'): continue
                            # Skipping type check for brevity, C checked REG/DIR

                            if count >= 10:
                                bits.append(" ... and more! ...\n")
                                break

                            bits.append(entry + "\n")
                            count += 1

                        content = "".join(bits).encode('utf-8')
                        self.send_preview_data(preview_id, path, content)
                    except OSError as e:
                        self.send_preview_error(preview_id, path,
                                                f"error: unable to open directory -- {e.strerror}")
                else:
                    self.send_preview_error(preview_id, path,
                                            f"error: path is not a file or directory -- {path}")

            except OSError as e:
                self.send_preview_error(preview_id, path, f"error: cannot open {path} -- {e.strerror}")

    def handle_complete_path_request(self, path: str):
        glob_path = path
        sys.stderr.write(f"complete path: {path}\n")

        try:
            if not path.endswith('/') and os.path.isdir(path):
                glob_path += "/"
            if not path.endswith('*'):
                glob_path += "*"

            sys.stderr.write(f"complete glob path: {glob_path}\n")
            self.send_possible_paths(glob_path, 0)
        except OSError:
            pass

    # --- Input Reading Helpers ---

    def read_full(self, fd: int, length: int) -> Optional[bytes]:
        data = bytearray()
        while len(data) < length:
            try:
                chunk = os.read(fd, length - len(data))
                if not chunk:
                    return None  # EOF
                data.extend(chunk)
            except OSError:
                return None
        return bytes(data)

    def read_packet_type(self, fd: int) -> Optional[int]:
        data = self.read_full(fd, 4)
        if not data: return None
        return struct.unpack('i', data)[0]

    def read_payload_type(self, fd: int) -> Optional[int]:
        data = self.read_full(fd, 4)
        if not data: return None
        return struct.unpack('i', data)[0]

    def read_string(self, fd: int) -> Optional[str]:
        # Assumes type already read or next is type? 
        # C readstr reads TYPE then LENGTH then BYTES.
        # This helper assumes the caller checked the TYPE.
        pl_type = self.read_payload_type(fd)
        if pl_type != TailerPacketPayloadType.TPPT_STRING:
            sys.stderr.write(f"error: expected string, got {pl_type}\n")
            self.running = False
            return None

        len_data = self.read_full(fd, 4)
        if not len_data: return None
        length = struct.unpack('i', len_data)[0]

        str_data = self.read_full(fd, length)
        if not str_data: return None
        return str_data.decode('utf-8', errors='replace')

    def read_int64(self, fd: int) -> Optional[int]:
        pl_type = self.read_payload_type(fd)
        if pl_type != TailerPacketPayloadType.TPPT_INT64:
            sys.stderr.write("error: expected int64\n")
            self.running = False
            return None
        data = self.read_full(fd, 8)
        if not data: return None
        return struct.unpack('q', data)[0]

    # --- Main Loop ---

    def run(self):
        # Announce system info (simulated)
        try:
            import platform
            sys_info = " ".join(platform.uname())
            send_packet(sys.stdout.fileno(),
                        TailerPacketType.TPT_ANNOUNCE,
                        (TailerPacketPayloadType.TPPT_STRING, sys_info))
        except Exception:
            pass

        stdin_fd = sys.stdin.fileno()
        # Ensure stdin is blocking or handle accordingly. C uses poll.

        rstate = RecvState.RS_PACKET_TYPE
        timeout = 0

        while self.running:
            r, _, _ = select.select([stdin_fd], [], [], timeout / 1000.0)

            if stdin_fd in r:
                ptype = self.read_packet_type(stdin_fd)

                if ptype is None:
                    sys.stderr.write("info: exiting...\n")
                    self.running = False
                    break

                # In C implementation, readstr/readint checks payload type internaly.
                # Here we emulate the flow.

                if ptype in (TailerPacketType.TPT_OPEN_PATH,
                             TailerPacketType.TPT_CLOSE_PATH,
                             TailerPacketType.TPT_LOAD_PREVIEW,
                             TailerPacketType.TPT_COMPLETE_PATH):

                    # Read String payload (Path)
                    path = self.read_string(stdin_fd)
                    preview_id = 0

                    if ptype == TailerPacketType.TPT_LOAD_PREVIEW:
                        preview_id = self.read_int64(stdin_fd)

                    # Done check
                    pl_type = self.read_payload_type(stdin_fd)
                    if pl_type != TailerPacketPayloadType.TPPT_DONE:
                        sys.stderr.write("error: invalid open packet\n")
                        self.running = False
                        break

                    if path:
                        if ptype == TailerPacketType.TPT_OPEN_PATH:
                            cps = self.find_client_path_state(self.client_path_list, path)
                            if cps:
                                sys.stderr.write(f"warning: already monitoring -- {path}\n")
                            else:
                                cps = self.create_client_path_state(path)
                                sys.stderr.write(f"info: monitoring path: {path}\n")
                                self.client_path_list.append(cps)

                        elif ptype == TailerPacketType.TPT_CLOSE_PATH:
                            cps = self.find_client_path_state(
                                self.client_path_list, path)
                            if not cps:
                                sys.stderr.write(f"warning: path is not open: {path}\n")
                            else:
                                self.client_path_list.remove(cps)

                        elif ptype == TailerPacketType.TPT_LOAD_PREVIEW:
                            self.handle_load_preview_request(path, preview_id)

                        elif ptype == TailerPacketType.TPT_COMPLETE_PATH:
                            self.handle_complete_path_request(path)

                elif ptype in (TailerPacketType.TPT_ACK_BLOCK, TailerPacketType.TPT_NEED_BLOCK):
                    # Read Path
                    path = self.read_string(stdin_fd)

                    ack_offset = 0
                    ack_len = 0
                    client_size = 0

                    if ptype == TailerPacketType.TPT_ACK_BLOCK:
                        # Read offset
                        ack_offset = self.read_int64(stdin_fd)
                        # Read len
                        ack_len = self.read_int64(stdin_fd)
                        # Read size
                        client_size = self.read_int64(stdin_fd)

                    pl_type = self.read_payload_type(stdin_fd)  # Done
                    if pl_type != TailerPacketPayloadType.TPPT_DONE:
                        sys.stderr.write("error: invalid open packet\n")
                        self.running = False
                        break

                    if path:
                        cps = self.find_client_path_state(self.client_path_list,
                                                          path)
                        if not cps:
                            sys.stderr.write(f"warning: unknown path in block packet: {path}\n")
                        elif ptype == TailerPacketType.TPT_NEED_BLOCK:
                            sys.stderr.write(f"info: client is tailing: {path}\n")
                            cps.cps_client_state = ClientState.CS_TAILING
                        elif ptype == TailerPacketType.TPT_ACK_BLOCK:
                            sys.stderr.write(f"info: client acked: {path} {client_size}\n")
                            if ack_len == 0:
                                cps.cps_client_state = ClientState.CS_TAILING
                            else:
                                cps.cps_client_file_offset = ack_offset + ack_len
                                cps.cps_client_state = ClientState.CS_INIT
                                cps.cps_client_file_size = client_size

            if self.running:
                if self.poll_paths(self.client_path_list):
                    timeout = 0
                else:
                    timeout = 1000


if __name__ == "__main__":
    # Clean up self if run with args (mimicking C behavior)
    if len(sys.argv) == 1:
        try:
            os.unlink(sys.argv[0])
        except OSError:
            pass

    app = Tailer()
    app.run()
