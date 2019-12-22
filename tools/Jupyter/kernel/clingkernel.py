#!/usr/bin/env python
#------------------------------------------------------------------------------
# CLING - the C++ LLVM-based InterpreterG :)
# author:  Min RK
# Copyright (c) Min RK
#
# This file is dual-licensed: you can choose to license it under the University
# of Illinois Open Source License or the GNU Lesser General Public License. See
# LICENSE.TXT for details.
#------------------------------------------------------------------------------

"""
Cling Kernel for Jupyter

Talks to Cling via ctypes
"""

from __future__ import print_function

__version__ = '0.0.3'

import ctypes
from contextlib import contextmanager
from fcntl import fcntl, F_GETFL, F_SETFL
import os
import shutil
import select
import struct
import sys
import threading

from traitlets import Unicode, Float, Dict, List, CaselessStrEnum
from ipykernel.kernelbase import Kernel
from ipykernel.kernelapp import kernel_aliases,kernel_flags, IPKernelApp
from ipykernel.ipkernel import IPythonKernel
from ipykernel.zmqshell import ZMQInteractiveShell
from IPython.core.profiledir import ProfileDir
from jupyter_client.session import Session


class my_void_p(ctypes.c_void_p):
  pass

libc = ctypes.CDLL(None)
try:
    c_stdout_p = ctypes.c_void_p.in_dll(libc, 'stdout')
    c_stderr_p = ctypes.c_void_p.in_dll(libc, 'stderr')
except ValueError:
    # libc.stdout is has a funny name on OS X
    c_stdout_p = ctypes.c_void_p.in_dll(libc, '__stdoutp')
    c_stderr_p = ctypes.c_void_p.in_dll(libc, '__stderrp')


class FdReplacer:
    """Stream replacement by pipes."""
    def __init__(self, name):
        self.name = name
        self.real_fd = getattr(sys, '__%s__' % name).fileno()
        self.save_fd = os.dup(self.real_fd)
        self.pipe_out, pipe_in = os.pipe()
        os.dup2(pipe_in, self.real_fd)
        os.close(pipe_in)
        # make pipe_out non-blocking
        flags = fcntl(self.pipe_out, F_GETFL)
        fcntl(self.pipe_out, F_SETFL, flags|os.O_NONBLOCK)

    def restore(self):
        os.close(self.real_fd)
        # and restore original stdout/stderr
        os.close(self.pipe_out)
        os.dup2(self.save_fd, self.real_fd)
        os.close(self.save_fd)


class ClingKernel(Kernel):
    """Cling Kernel for Jupyter"""
    implementation = 'cling_kernel'
    implementation_version = __version__
    language_version = 'X'

    banner = Unicode()
    def _banner_default(self):
        return 'cling-%s' % self.language_version
        return self._banner

    language_info = {'name': 'c++',
                     'codemirror_mode': 'c++',
                     'mimetype': 'text/x-c++src',
                     'file_extension': '.c++'}

    # Used in handle_input()
    flush_interval = Float(0.25, config=True)

    std = CaselessStrEnum(default_value='c++11',
            values = ['c++11', 'c++14', 'c++1z', 'c++17'],
            help="C++ standard to use, either c++17, c++1z, c++14 or c++11").tag(config=True);

    def __init__(self, **kwargs):
        super(ClingKernel, self).__init__(**kwargs)
        try:
            clingInPath = shutil.which('cling')
        except AttributeError:
            clingInPath = None
        if not clingInPath:
            from distutils.spawn import find_executable
            clingInPath = find_executable('cling')
        if not clingInPath:
            raise RuntimeError('Cannot find cling in $PATH. No cling, no fun.')

        try:
            whichCling = os.readlink(clingInPath)
            whichCling = os.path.join(os.path.dirname(clingInPath), whichCling)
        except OSError as e:
            #If cling is not a symlink try a regular file
            #readlink returns POSIX error EINVAL (22) if the
            #argument is not a symlink
            if e.args[0] == 22:
                whichCling = clingInPath
            else:
                raise e

        if whichCling:
            clingInstDir = os.path.abspath(os.path.dirname(os.path.dirname(whichCling)))
            llvmResourceDir = clingInstDir
        else:
            raise RuntimeError('cling at ' + clingInPath + ' is unusable. No cling, no fun.')

        for libFolder in ["/lib/libclingJupyter.", "/libexec/lib/libclingJupyter."]:

            for ext in ['so', 'dylib', 'dll']:
                libFilename = clingInstDir + libFolder + ext
                if os.access(libFilename, os.R_OK):
                    self.libclingJupyter = ctypes.CDLL(clingInstDir + libFolder + ext,
                                                    mode = ctypes.RTLD_GLOBAL)
                    break
            else:
                continue
            break

        if not getattr(self, 'libclingJupyter', None):
            raise RuntimeError('Cannot find ' + clingInstDir + '/lib/libclingJupyter.{so,dylib,dll}')

        self.libclingJupyter.cling_create.restype = my_void_p
        self.libclingJupyter.cling_eval.restype = my_void_p
        #build -std=c++11 or -std=c++14 option
        stdopt = ("-std=" + self.std).encode('utf-8')
        self.log.info("Using {}".format(stdopt.decode('utf-8')))
        #from IPython.utils import io
        #io.rprint("DBG: Using {}".format(stdopt.decode('utf-8')))
        argv = [b"clingJupyter", stdopt, b"-I" + clingInstDir.encode('utf-8') + b"/include/"]

        # Environment variable CLING_OPTS used to pass arguments to cling
        extra_opts = os.getenv('CLING_OPTS')
        if extra_opts:
            for x in extra_opts.split():
                argv.append(x.encode('utf-8'))
                self.log.info("Passing extra argument {} to cling".format(x))

        argc = len(argv)
        CharPtrArrayType = ctypes.c_char_p * argc

        llvmResourceDirCP = ctypes.c_char_p(llvmResourceDir.encode('utf8'))

        # The sideband_pipe is used by cling::Jupyter::pushOutput() to publish MIME data to Jupyter.
        self.sideband_pipe, pipe_in = os.pipe()
        self.interp = self.libclingJupyter.cling_create(ctypes.c_int(argc), CharPtrArrayType(*argv), llvmResourceDirCP, pipe_in)

        self.libclingJupyter.cling_complete_start.restype = my_void_p
        self.libclingJupyter.cling_complete_next.restype = my_void_p #c_char_p

    def _process_stdio_data(self, pipe, name):
        """Read from the pipe, send it to IOPub as name stream."""
        data = os.read(pipe, 1024)
        # send output
        self.session.send(self.iopub_socket, 'stream', {
          'name': name,
          'text': data.decode('utf8', 'replace'),
        }, parent=self._parent_header)

    def _recv_dict(self, pipe):
        """Receive a serialized dict on a pipe

        Returns the dictionary.
        """
        # Wire format:
        #   // Pipe sees (all numbers are longs, except for the first):
        #   // - num bytes in a long (sent as a single unsigned char!)
        #   // - num elements of the MIME dictionary; Jupyter selects one to display.
        #   // For each MIME dictionary element:
        #   //   - length of MIME type key
        #   //   - MIME type key
        #   //   - size of MIME data buffer (including the terminating 0 for
        #   //     0-terminated strings)
        #   //   - MIME data buffer
        data = {}
        b1 = os.read(pipe, 1)
        sizeof_long = struct.unpack('B', b1)[0]
        if sizeof_long == 8:
            fmt = 'Q'
        else:
            fmt = 'L'
        buf = os.read(pipe, sizeof_long)
        num_elements = struct.unpack(fmt, buf)[0]
        for i in range(num_elements):
            buf = os.read(pipe, sizeof_long)
            len_key = struct.unpack(fmt, buf)[0]
            key = os.read(pipe, len_key).decode('utf8')
            buf = os.read(pipe, sizeof_long)
            len_value = struct.unpack(fmt, buf)[0]
            value = os.read(pipe, len_value).decode('utf8')
            data[key] = value
        return data


    def _process_sideband_data(self):
        """publish display-data messages on IOPub.
        """
        data = self._recv_dict(self.sideband_pipe)
        self.session.send(self.iopub_socket, 'display_data',
            content={
                'data': data,
                'metadata': {},
            },
            parent=self._parent_header,
            )

    def forward_streams(self):
        """Put the forwarding pipes in place for stdout, stderr."""
        self.replaced_streams = [FdReplacer("stdout"), FdReplacer("stderr")]

    def handle_input(self):
        """Capture stdout, stderr and sideband. Forward them as stream messages."""
        # create pipe for stdout, stderr
        select_on = [self.sideband_pipe]
        for rs in self.replaced_streams:
            if rs:
                select_on.append(rs.pipe_out)

        r, w, x = select.select(select_on, [], [], self.flush_interval)
        if not r:
            # nothing to read, flush libc's stdout and check again
            libc.fflush(c_stdout_p)
            libc.fflush(c_stderr_p)
            return False

        for fd in r:
            if fd == self.sideband_pipe:
                self._process_sideband_data()
            else:
                if fd == self.replaced_streams[0].pipe_out:
                    rs = 0
                else:
                    rs = 1
                self._process_stdio_data(fd, self.replaced_streams[rs].name)
        return True

    def close_forwards(self):
        """Close the forwarding pipes."""
        libc.fflush(c_stdout_p)
        libc.fflush(c_stderr_p)
        for rs in self.replaced_streams:
            rs.restore()
        self.replaced_streams = []

    def run_cell(self, code, silent=False):
        """Run code in cling, storing the expression result or an empty string if there is none."""
        self.stringResult = self.libclingJupyter.cling_eval(self.interp, ctypes.c_char_p(code.encode('utf8')))

    def do_execute(self, code, silent, store_history=True,
                   user_expressions=None, allow_stdin=False):
        """Runs code in cling and handles input; returns the evaluation result."""
        if not code.strip():
            return {
                'status': 'ok',
                'execution_count': self.execution_count,
                'payload': [],
                'user_expressions': {},
            }

        # Redirect stdout, stderr so handle_input() can pick it up.
        self.forward_streams()

        # Run code in cling in a thread.
        run_cell_thread = threading.Thread(target=self.run_cell, args=(code, silent,))
        run_cell_thread.start()
        while True:
            self.handle_input()
            if not run_cell_thread.is_alive():
                # self.run_cell() has returned.
                break

        run_cell_thread.join()

        # Any leftovers?
        while self.handle_input(): True

        self.close_forwards()
        status = 'ok'
        if not self.stringResult:
            status = 'error'
        else:
            # Execution has finished; we have a result.
            self.session.send(
                self.iopub_socket,
                'execute_result',
                content={
                    'data': {
                        'text/plain': ctypes.cast(self.stringResult, ctypes.c_char_p).value.decode('utf8', 'replace'),
                    },
                    'metadata': {},
                    'execution_count': self.execution_count,
                },
                parent=self._parent_header
            )
            self.libclingJupyter.cling_eval_free(self.stringResult)


        reply = {
            'status': status,
            'execution_count': self.execution_count,
        }

        if status == 'error':
            err = {
                'ename': 'ename',
                'evalue': 'evalue',
                'traceback': [],
            }
            self.send_response(self.iopub_socket, 'error', err)
            reply.update(err)
        elif status == 'ok':
            reply.update({
                'THIS DOES NOT WORK: payload': [{
                  'source': 'set_next_input',
                  'replace': True,
                  'text':'//THIS IS MAGIC\n' + code
                }],
                'user_expressions': {},
            })
        else:
            raise ValueError("Invalid status: %r" % status)

        return reply

    def do_complete(self, code, cursor_pos):
        """Provide completions here"""
        # if cursor_pos = cursor_start = cursor_end,
        # matches should be a list of strings to be appended after the cursor
        return {'matches' : [],
                'cursor_end' : cursor_pos,
                'cursor_start' : cursor_pos,
                'metadata' : {},
                'status' : 'ok'}

cling_flags = kernel_flags
class ClingKernelApp(IPKernelApp):
    name='cling-kernel'
    cling_aliases = kernel_aliases.copy()
    cling_aliases['std']='ClingKernel.std'
    aliases = Dict(cling_aliases)
    flags = Dict(cling_flags)
    classes = List([ ClingKernel, IPythonKernel, ZMQInteractiveShell, ProfileDir, Session ])
    kernel_class = ClingKernel

def main():
    """launch a cling kernel"""
    ClingKernelApp.launch_instance()


if __name__ == '__main__':
    main()
