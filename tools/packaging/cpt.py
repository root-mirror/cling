#! /usr/bin/env python
# coding:utf-8

###############################################################################
#
#                           The Cling Interpreter
#
# Cling Packaging Tool (CPT)
#
# tools/packaging/cpt.py: Python script to launch Cling Packaging Tool (CPT)
#
# Documentation: tools/packaging/README.md
#
# Author: Anirudha Bose <ani07nov@gmail.com>
#
# This file is dual-licensed: you can choose to license it under the University
# of Illinois Open Source License or the GNU Lesser General Public License. See
# LICENSE.TXT for details.
#
###############################################################################

# Python 2 and Python 3 compatibility
from __future__ import print_function
import sys
import argparse
import os
import platform
import subprocess
import shutil
import shlex
import glob
import re
import tarfile
import zipfile
from email.utils import formatdate
from datetime import tzinfo
import time
import multiprocessing
import stat
import json
if sys.version_info < (3, 0):
    # Python 2.x
    from urllib2 import urlopen
    input = raw_input
else:
    # Python 3.x
    from urllib.request import urlopen


###############################################################################
#              Platform independent functions (formerly indep.py)             #
###############################################################################

def _convert_subprocess_cmd(cmd):
    if OS == 'Windows':
        cmd = cmd.replace('\\','/')
    return shlex.split(cmd, posix=True, comments=True)


def _perror(e):
    print("subprocess.CalledProcessError: Command '%s' returned non-zero exit status %s" % (
        ' '.join(e.cmd), str(e.returncode)))
    cleanup()
    # Communicate return code to the calling program if any
    sys.exit(e.returncode)


def exec_subprocess_call(cmd, cwd, showCMD=False):
    if showCMD: print(cmd)
    cmd = _convert_subprocess_cmd(cmd)
    try:
        subprocess.check_call(cmd, cwd=cwd, shell=False,
                              stdin=subprocess.PIPE, stdout=None, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        _perror(e)


def exec_subprocess_check_output(cmd, cwd):
    cmd = _convert_subprocess_cmd(cmd)
    out = ''
    try:
        out = subprocess.check_output(cmd, cwd=cwd, shell=False,
                                      stdin=subprocess.PIPE, stderr=subprocess.STDOUT).decode('utf-8')
    except subprocess.CalledProcessError as e:
        _perror(e)
    finally:
        return out

def travis_fold_start(tag):
    if os.environ.get('TRAVIS_BUILD_DIR', None):
       print('travis_fold:start:cpt-%s:' % (tag))

def travis_fold_end(tag):
   if os.environ.get('TRAVIS_BUILD_DIR', None):
      print('travis_fold:end:cpt-%s:' % (tag))

def box_draw_header():
    msg = 'cling (' + platform.machine() + ')' + formatdate(time.time(), tzinfo())
    spaces_no = 80 - len(msg) - 4
    spacer = ' ' * spaces_no
    msg = 'cling (' + platform.machine() + ')' + spacer + formatdate(time.time(), tzinfo())

    if OS != 'Windows':
        print('''
╔══════════════════════════════════════════════════════════════════════════════╗
║ %s ║
╚══════════════════════════════════════════════════════════════════════════════╝''' % (msg))
    else:
        print('''
+=============================================================================+
| %s|
+=============================================================================+''' % (msg))


def box_draw(msg):
    spaces_no = 80 - len(msg) - 4
    spacer = ' ' * spaces_no

    if OS == 'Linux':
        print('''
┌──────────────────────────────────────────────────────────────────────────────┐
│ %s%s │
└──────────────────────────────────────────────────────────────────────────────┘''' % (msg, spacer))
    else:
        print('''
+-----------------------------------------------------------------------------+
| %s%s|
+-----------------------------------------------------------------------------+''' % (msg, spacer))


def pip_install(package):
    # Needs brew install python. We should only install if we need the
    # functionality
    import pip
    pip.main(['install', '--ignore-installed', '--prefix', os.path.join(workdir, 'pip'), '--upgrade', package])


def wget(url, out_dir, rename_file=None, retries=3):
    file_name = url.split('/')[-1]
    print("  HTTP request sent, awaiting response ... ")
    u = urlopen(url)
    if u.code != 200 or retries == 0:
        exit()
    else:
        print("  Connected to %s [200 OK]" % (url))

    try:
        file_size = u.headers.get('Content-Length')
        if file_size:
            file_size = int(file_size)
        else:
            raise Exception
    except Exception:
        print('  Error due to broken pipe')
        print('  Retrying ...')
        wget(url, out_dir, retries-1)

    else:
        print("  Downloading: %s Bytes: %s" % (file_name, file_size))

        file_size_dl = 0
        block_sz = 8192

        f = open(os.path.join(out_dir, file_name), 'wb')

        while True:
            buffer = u.read(block_sz)
            if not buffer:
                break

            file_size_dl += len(buffer)
            f.write(buffer)
            status = r"%10d  [%3.2f%%]" % (file_size_dl, file_size_dl * 100. / file_size)
            status += chr(8) * (len(status) + 1)
            print(status, end=' ')
        f.close()
        if rename_file:
            ffrom = os.path.join(out_dir, file_name)
            fto = os.path.join(out_dir, rename_file)
            print('Moving file: ' + ffrom + ' -> ' + fto)
            os.rename(ffrom, fto)
        print()


def fetch_llvm(llvm_revision):
    box_draw("Fetch source files")
    print('Last known good LLVM revision is: ' + llvm_revision)
    print('Current working directory is: ' + workdir + '\n')

    if "github.com" in LLVM_GIT_URL and args['create_dev_env'] is None and args['use_wget']:
        _, _, _, user, repo = LLVM_GIT_URL.split('/')
        print('Fetching LLVM ...')
        wget(url='https://github.com/%s/%s' % (user, repo.replace('.git', '')) +
                 '/archive/cling-patches-r%s.tar.gz' % llvm_revision,
             out_dir=workdir)

        print('Extracting: ' + os.path.join(workdir, 'cling-patches-r%s.tar.gz' % llvm_revision))
        tar = tarfile.open(os.path.join(workdir, 'cling-patches-r%s.tar.gz' % llvm_revision))
        tar.extractall(path=workdir)
        tar.close()

        os.rename(os.path.join(workdir, 'llvm-cling-patches-r%s' % llvm_revision), srcdir)

        if os.path.isfile(os.path.join(workdir, 'cling-patches-r%s.tar.gz' % llvm_revision)):
            print("Remove file: " + os.path.join(workdir, 'cling-patches-r%s.tar.gz' % llvm_revision))
            os.remove(os.path.join(workdir, 'cling-patches-r%s.tar.gz' % llvm_revision))

        print()
        return

    def checkout():
        if LLVM_BRANCH:
            exec_subprocess_call('git checkout %s' % LLVM_BRANCH, srcdir)
        else:
            exec_subprocess_call('git checkout cling-patches-r%s' % llvm_revision, srcdir)

    def get_fresh_llvm():
        if LLVM_BRANCH:
            exec_subprocess_call('git clone --depth=10 --branch %s %s %s'
                                 % (LLVM_BRANCH, LLVM_GIT_URL, srcdir), workdir)
        else:
            exec_subprocess_call('git clone %s %s' % (LLVM_GIT_URL, srcdir), workdir)

        checkout()

    def update_old_llvm():
        exec_subprocess_call('git stash', srcdir)

        # exec_subprocess_call('git clean -f -x -d', srcdir)

        checkout()

        if LLVM_BRANCH:
            exec_subprocess_call('git pull origin %s' % LLVM_BRANCH, srcdir)
        else:
            exec_subprocess_call('git fetch --tags', srcdir)
            exec_subprocess_call('git pull origin refs/tags/cling-patches-r%s'
                                 % llvm_revision, srcdir)

    if os.path.isdir(srcdir):
        update_old_llvm()
    else:
        get_fresh_llvm()

# TODO Refactor all fetch_ functions to use this class will remove a lot of dup
class RepoCache(object):
    def __init__(self, url, rootDir, depth=10):
        self.__url = url
        self.__depth = depth
        self.__projDir = rootDir
        self.__workDir = os.path.join(rootDir, url.split('/')[-1])
    def fetch(self, branch):
        if os.path.isdir(self.__workDir):
            exec_subprocess_call('git stash', self.__workDir)
            exec_subprocess_call('git clean -f -x -d', self.__workDir)
            exec_subprocess_call('git fetch --tags', self.__workDir)
        else:
            exec_subprocess_call('git clone %s' % self.__url, self.__projDir)

        exec_subprocess_call('git checkout %s' % branch, self.__workDir)

def should_fetch_libcpp(llvm_revision):
    stdlib = args.get('stdlib')
    if stdlib.startswith('libc++'):
        # -stdlib=libc++,release_39 means build libc++,release_39
        # otherwise use sytem provided
        # we return the revision and caller should know what to do with it
        # set the arg to 'libc++' so that can be used as a test
        args['stdlib'] = 'libc++'
        return stdlib[7:]
    return False

def fetch_libcpp(llvm_revision, libcpp_tag):
    if libcpp_tag:
        # TODO make sure this tag has common ancestor with llvm_revision
        projdir = os.path.join(srcdir, 'projects')
        RepoCache('https://github.com/llvm-mirror/libcxx', projdir).fetch(libcpp_tag)
        RepoCache('https://github.com/llvm-mirror/libcxxabi', projdir).fetch(libcpp_tag)
    return True

def fetch_clang(llvm_revision):
    if "github.com" in CLANG_GIT_URL and args['create_dev_env'] is None and args['use_wget']:
        _, _, _, user, repo = CLANG_GIT_URL.split('/')
        print('Fetching Clang ...')
        wget(url='https://github.com/%s/%s' % (user, repo.replace('.git', '')) +
                 '/archive/cling-patches-r%s.tar.gz' % llvm_revision,
             out_dir=workdir)

        print('Extracting: ' + os.path.join(workdir, 'cling-patches-r%s.tar.gz' % llvm_revision))
        tar = tarfile.open(os.path.join(workdir, 'cling-patches-r%s.tar.gz' % llvm_revision))
        tar.extractall(path=os.path.join(srcdir, 'tools'))
        tar.close()

        os.rename(os.path.join(srcdir, 'tools', 'clang-cling-patches-r%s' % llvm_revision),
                  os.path.join(srcdir, 'tools', 'clang'))

        if os.path.isfile(os.path.join(workdir, 'cling-patches-r%s.tar.gz' % llvm_revision)):
            print("Remove file: " + os.path.join(workdir, 'cling-patches-r%s.tar.gz' % llvm_revision))
            os.remove(os.path.join(workdir, 'cling-patches-r%s.tar.gz' % llvm_revision))

        print()
        return

    toolsdir = os.path.join(srcdir, 'tools')
    clangdir = os.path.join(toolsdir, 'clang')
    def checkout():
       if CLANG_BRANCH:
           exec_subprocess_call('git checkout %s' % CLANG_BRANCH, clangdir)
       else:
           exec_subprocess_call('git checkout cling-patches-r%s' % llvm_revision, clangdir)

    def get_fresh_clang():
        if CLANG_BRANCH:
            exec_subprocess_call('git clone --depth=10 --branch %s %s'
                                 % (CLANG_BRANCH, CLANG_GIT_URL), toolsdir)
        else:
            exec_subprocess_call('git clone %s' % CLANG_GIT_URL, toolsdir)

        checkout()

    def update_old_clang():
        exec_subprocess_call('git stash', clangdir)

        # exec_subprocess_call('git clean -f -x -d', clangdir)

        exec_subprocess_call('git fetch --tags', clangdir)

        checkout()
        if CLANG_BRANCH:
            exec_subprocess_call('git pull origin %s' % CLANG_BRANCH, clangdir)
        else:
            exec_subprocess_call('git fetch --tags', clangdir)
            exec_subprocess_call('git pull origin refs/tags/cling-patches-r%s' % llvm_revision,
                                  clangdir)

    if os.path.isdir(clangdir):
        update_old_clang()
    else:
        get_fresh_clang()


def fetch_cling(arg):
    def get_fresh_cling():
        if CLING_BRANCH:
            exec_subprocess_call('git clone --depth=10 --branch %s %s'
                                 % (CLING_BRANCH, CLING_GIT_URL), os.path.join(srcdir, 'tools'))
        else:
            exec_subprocess_call('git clone %s' % CLING_GIT_URL, os.path.join(srcdir, 'tools'))

        # if arg == 'last-stable':
        #    checkout_branch = exec_subprocess_check_output('git describe --match v* --abbrev=0 --tags | head -n 1',
        #                                                   CLING_SRC_DIR)

        if arg == 'master':
            checkout_branch = 'master'
        else:
            checkout_branch = arg

        exec_subprocess_call('git checkout %s' % checkout_branch, CLING_SRC_DIR)

    def update_old_cling():
        # exec_subprocess_call('git stash', CLING_SRC_DIR)

        # exec_subprocess_call('git clean -f -x -d', CLING_SRC_DIR)

        exec_subprocess_call('git fetch --tags', CLING_SRC_DIR)

        # if arg == 'last-stable':
        #    checkout_branch = exec_subprocess_check_output('git describe --match v* --abbrev=0 --tags | head -n 1',
        #                                                   CLING_SRC_DIR)

        if arg == 'master':
            checkout_branch = 'master'
        else:
            checkout_branch = arg

        exec_subprocess_call('git checkout %s' % checkout_branch, CLING_SRC_DIR)

        exec_subprocess_call('git pull origin %s' % checkout_branch, CLING_SRC_DIR)

    if os.path.isdir(CLING_SRC_DIR):
        update_old_cling()
    else:
        get_fresh_cling()


def set_version():
    global VERSION
    global REVISION
    box_draw("Set Cling version")
    VERSION = open(os.path.join(CLING_SRC_DIR, 'VERSION'), 'r').readline().strip()

    # If development release, then add revision to the version
    REVISION = exec_subprocess_check_output('git log -n 1 --pretty=format:%H', CLING_SRC_DIR).strip()

    if '~dev' in VERSION:
        VERSION = VERSION + '-' + REVISION[:7]

    print('Version: ' + VERSION)
    print('Revision: ' + REVISION)


def set_vars():
    global EXEEXT
    global SHLIBEXT
    global CLANG_VERSION
    box_draw("Set variables")
    if not os.path.isfile(os.path.join(LLVM_OBJ_ROOT, 'test', 'lit.site.cfg')):
        exec_subprocess_call('make lit.site.cfg', os.path.join(LLVM_OBJ_ROOT, 'test'))

    with open(os.path.join(LLVM_OBJ_ROOT, 'test', 'lit.site.cfg'), 'r') as lit_site_cfg:
        for line in lit_site_cfg:
            if re.match('^config.llvm_shlib_ext = ', line):
                SHLIBEXT = re.sub('^config.llvm_shlib_ext = ', '', line).replace('"', '').strip()
            elif re.match('^config.llvm_exe_ext = ', line):
                EXEEXT = re.sub('^config.llvm_exe_ext = ', '', line).replace('"', '').strip()

    if not os.path.isfile(os.path.join(LLVM_OBJ_ROOT, 'tools', 'clang', 'include', 'clang', 'Basic', 'Version.inc')):
        exec_subprocess_call('make Version.inc',
                             os.path.join(LLVM_OBJ_ROOT, 'tools', 'clang', 'include', 'clang', 'Basic'))

    with open(os.path.join(LLVM_OBJ_ROOT, 'tools', 'clang', 'include', 'clang', 'Basic', 'Version.inc'),
              'r') as Version_inc:
        for line in Version_inc:
            if re.match('^#define CLANG_VERSION ', line):
                CLANG_VERSION = re.sub('^#define CLANG_VERSION ', '', line).strip()

    print('EXEEXT: ' + EXEEXT)
    print('SHLIBEXT: ' + SHLIBEXT)
    print('CLANG_VERSION: ' + CLANG_VERSION)

class Build(object):
    def __init__(self, target=None):
        super(Build, self).__init__()
        if args.get('create_dev_env'):
            if args.get('create_dev_env') is None:
                self.buildType = 'Debug'
            else:
                self.buildType = args.get('create_dev_env')
        else:
            self.buildType = 'Release'
        self.win32 = platform.system() == 'Windows'
        self.cores = multiprocessing.cpu_count()
        # Travis CI, GCC crashes if more than 4 cores used.
        if os.environ.get('TRAVIS_OS_NAME', None):
            self.cores = min(self.cores, 4)
        if target:
            self.make(target)

    def config(self, configFlags=''):
        box_draw('Configure Cling with CMake ' + configFlags)
        exec_subprocess_call('%s %s' % (CMAKE, configFlags), LLVM_OBJ_ROOT, True)

    def make(self, targets, flags=''):
        box_draw('Building %s (using %d cores)' % (targets, self.cores))
        if self.win32:
            flags += ' --config %s' % self.buildType
            for target in targets.split():
                exec_subprocess_call('%s --build . --target %s %s'
                                     % (CMAKE, target, flags), LLVM_OBJ_ROOT)
        else:
            if args['verbose']: flags += ' VERBOSE=1'
            exec_subprocess_call('make -j%d %s %s' % (self.cores, targets, flags),
                                 LLVM_OBJ_ROOT)

def compile(arg, build_libcpp):
    travis_fold_start("compile")
    global prefix, EXTRA_CMAKE_FLAGS
    prefix = arg

    # Cleanup previous installation directory if any
    if os.path.isdir(prefix):
        print("Remove directory: " + prefix)
        shutil.rmtree(prefix)

    # Cleanup previous build directory if exists
    if os.path.isdir(LLVM_OBJ_ROOT):
        print("Using previous build directory: " + LLVM_OBJ_ROOT)
    else:
        print("Creating build directory: " + LLVM_OBJ_ROOT)
        os.makedirs(LLVM_OBJ_ROOT)

    ### FIX: Target isn't being set properly on Travis OS X
    ### Either because ccache or maybe the virtualization environment
    if TRAVIS_BUILD_DIR and OS == 'Darwin':
        triple = exec_subprocess_check_output('sh %s/cmake/config.guess' % srcdir, srcdir)
        if triple:
            EXTRA_CMAKE_FLAGS = ' -DLLVM_HOST_TRIPLE="%s" ' % triple.rstrip() + EXTRA_CMAKE_FLAGS

    build = Build()
    cmake_config_flags = (srcdir + ' -DLLVM_BUILD_TOOLS=Off -DCMAKE_BUILD_TYPE={0} -DCMAKE_INSTALL_PREFIX={1} '
                          .format(build.buildType, TMP_PREFIX) + ' -DLLVM_TARGETS_TO_BUILD="host;NVPTX" ' +
                          EXTRA_CMAKE_FLAGS)

    libcxx = ''
    stdlib = args.get('stdlib')
    if stdlib:
        libcxx = ' -DLLVM_ENABLE_LIBCXX=%s' % ('ON' if stdlib == 'libc++' else 'OFF')

    # Don't pollute the CCACHE_LOGFILE with CMake config
    CCACHE_LOGFILE = os.environ.get('CCACHE_LOGFILE', None)
    if CCACHE_LOGFILE:
        del os.environ['CCACHE_LOGFILE']

    if libcpp:
        # configure and build libc++
        build.config(cmake_config_flags)
        build.make('cxx')

        ''' ###TODO: This could easily be useful in Build class
        Only use case is here right now'''

        if build.win32:
            incFlag = '/I'
            linkFlags = '/LIBPATH:%s ' % os.path.join(LLVM_OBJ_ROOT, 'lib')
            linkFlags += os.path.join(LLVM_OBJ_ROOT, 'lib', 'libc++'+SHLIBEXT)
        else:
            incFlag = '-I'
            linkFlags = '-L%s ' % os.path.join(LLVM_OBJ_ROOT, 'lib')
            linkFlags += os.path.join(LLVM_OBJ_ROOT, 'lib', 'libc++'+SHLIBEXT)
            if OS == 'Linux':
                linkFlags += ' -lm'
                ### Fix clang-3.5 compiling clang-3.9
                if args['compiler'] == 'clang++-3.5':
                    cmake_config_flags += ' -DCMAKE_CXX_FLAGS_RELEASE="-O0 -DNDEBUG" '

        # Force LLVM, clang, and cling to use headers & link to 'local' libc++
        cmake_config_flags += ' -DCMAKE_SHARED_LINKER_FLAGS="%s"' % linkFlags
        cmake_config_flags += ' -DCMAKE_EXE_LINKER_FLAGS="%s"' % linkFlags
        cmake_config_flags += ' -DCMAKE_CXX_FLAGS="%s %s" ' % (incFlag,
                            os.path.join(LLVM_OBJ_ROOT, 'include', 'c++', 'v1'))

        # Don't build libcxx and libcxxabi again with linker flags above
        # OS X allows a lib to link to -itself- which inf-loops dyld at runtime
        projdir = os.path.join(srcdir, 'projects')
        shutil.rmtree(os.path.join(projdir, 'libcxx'))
        shutil.rmtree(os.path.join(projdir, 'libcxxabi'))

    # configure cling
    build.config(cmake_config_flags + libcxx)

    # Start the logging, we currently don't log libcpp being built above
    if CCACHE_LOGFILE:
        os.environ['CCACHE_LOGFILE'] = CCACHE_LOGFILE

    build.make('clang cling' if CLING_BRANCH else 'cling')

    if not CLING_BRANCH:
        box_draw("Install compiled binaries to prefix (using %d cores)" % build.cores)
        build.make('install')

    if TRAVIS_BUILD_DIR:
        ### Run cling once, dumping the include paths, helps debug issues
        try:
            subprocess.check_call(os.path.join(workdir, 'builddir', 'bin', 'cling')
                                  + ' -v ".I"', shell=True)
        except Exception as e:
            print(e)
    travis_fold_end("compile")

def install_prefix():
    travis_fold_start("install")
    global prefix
    set_vars()

    box_draw("Filtering Cling's libraries and binaries")

    regex_array = []
    regex_filename = os.path.join(CPT_SRC_DIR, 'dist-files.txt')
    for line in open(regex_filename).read().splitlines():
      if line and not line.startswith('#'):
        regex_array.append(line)

    for root, dirs, files in os.walk(TMP_PREFIX):
        for file in files:
            f = os.path.join(root, file).replace(TMP_PREFIX, '')
            if OS == 'Windows':
                f = f.replace('\\', '/')
            for regex in regex_array:
                if args['verbose']: print ("Applying regex " + regex + " to file " + f)
                if re.search(regex, f):
                    print ("Adding to final binary " + f)
                    if not os.path.isdir(os.path.join(prefix, os.path.dirname(f))):
                        os.makedirs(os.path.join(prefix, os.path.dirname(f)))
                    shutil.copy(os.path.join(TMP_PREFIX, f), os.path.join(prefix, f))
                    break
    travis_fold_end("install")


def runSingleTest(test, Idx = 2, Recurse = True):
    try:
        test = os.path.join(CLING_SRC_DIR, 'test', test)

        if os.path.isdir(test):
            if Recurse:
                for t in os.listdir(test):
                    if t.endswith('.C'):
                        runSingleTest(os.path.join(test, t), Idx, False)
            return

        cling = os.path.join(LLVM_OBJ_ROOT, 'bin', 'cling')
        flags = [[''], ['-Xclang -verify']]
        flags.append([f[0] for f in flags])
        for flag in flags[Idx]:
            cmd = 'cat %s | %s --nologo 2>&1 %s' % (test, cling, flag)
            print('** %s **' % cmd)
            subprocess.check_call(cmd, cwd=os.path.dirname(test), shell=True)

    except Exception as err:
        print("Error running '%s': %s" % (test, err))
        pass

def test_cling():
    box_draw("Run Cling test suite")
    # Run single tests on CI with this
    # runSingleTest('Prompt/ValuePrinter/Regression.C')
    # runSingleTest('Prompt/ValuePrinter')
    Build('check-cling')

def tarball():
    box_draw("Compress binaries into a bzip2 tarball")
    tar = tarfile.open(prefix + '.tar.bz2', 'w:bz2')
    print('Creating archive: ' + os.path.basename(prefix) + '.tar.bz2')
    tar.add(prefix, arcname=os.path.basename(prefix))
    tar.close()

gInCleanup = False
def cleanup():
    global gInCleanup
    if gInCleanup:
        print('Failure in cleanup lead to recursion\n')
        return

    gInCleanup = True
    print('\n')
    if args['skip_cleanup']:
        box_draw("Skipping cleanup")
        return

    box_draw("Clean up")
    if os.path.isdir(LLVM_OBJ_ROOT):
        print("Skipping build directory: " + LLVM_OBJ_ROOT)

    if os.path.isdir(prefix):
        print("Remove directory: " + prefix)
        shutil.rmtree(prefix)

    if os.path.isdir(TMP_PREFIX):
        print("Remove directory: " + TMP_PREFIX)
        shutil.rmtree(TMP_PREFIX)

    if os.path.isfile(os.path.join(workdir, 'cling.nsi')):
        print("Remove file: " + os.path.join(workdir, 'cling.nsi'))
        os.remove(os.path.join(workdir, 'cling.nsi'))

    if args['current_dev'] == 'deb' or args['last_stable'] == 'deb' or args['deb_tag']:
        print('Create output directory: ' + os.path.join(workdir, 'cling-%s-1' % (VERSION)))
        os.makedirs(os.path.join(workdir, 'cling-%s-1' % (VERSION)))

        for file in glob.glob(os.path.join(workdir, 'cling_%s*' % (VERSION))):
            print(file + '->' + os.path.join(workdir, 'cling-%s-1' % (VERSION), os.path.basename(file)))
            shutil.move(file, os.path.join(workdir, 'cling-%s-1' % (VERSION)))

        if not os.listdir(os.path.join(workdir, 'cling-%s-1' % (VERSION))):
            os.rmdir(os.path.join(workdir, 'cling-%s-1' % (VERSION)))

    if args['current_dev'] == 'dmg' or args['last_stable'] == 'dmg' or args['dmg_tag']:
        if os.path.isfile(os.path.join(workdir, 'cling-%s-temp.dmg' % (VERSION))):
            print("Remove file: " + os.path.join(workdir, 'cling-%s-temp.dmg' % (VERSION)))
            os.remove(os.path.join(workdir, 'cling-%s-temp.dmg' % (VERSION)))

        if os.path.isdir(os.path.join(workdir, 'Cling.app')):
            print('Remove directory: ' + 'Cling.app')
            shutil.rmtree(os.path.join(workdir, 'Cling.app'))

        if os.path.isdir(os.path.join(workdir, 'cling-%s-temp.dmg' % (VERSION))):
            print('Remove directory: ' + os.path.join(workdir, 'cling-%s-temp.dmg' % (VERSION)))
            shutil.rmtree(os.path.join(workdir, 'cling-%s-temp.dmg' % (VERSION)))

        if os.path.isdir(os.path.join(workdir, 'Install')):
            print('Remove directory: ' + os.path.join(workdir, 'Install'))
            shutil.rmtree(os.path.join(workdir, 'Install'))
    gInCleanup = False

def check_version_string_ge(vstring, min_vstring):
    version_fields = [int(x) for x in vstring.split('.')]
    min_versions = [int(x) for x in min_vstring.split('.')]
    for i in range(0,len(min_versions)):
        if version_fields[i] < min_versions[i]:
            return False
        elif version_fields[i] > min_versions[i]:
            return True
    return True


###############################################################################
#            Debian specific functions (ported from debianize.sh)             #
###############################################################################

def check_ubuntu(pkg):
    if pkg == "gnupg":
        SIGNING_USER = exec_subprocess_check_output('gpg --fingerprint | grep uid | sed s/"uid *"//g', '/').strip()
        if SIGNING_USER == '':
            print(pkg.ljust(20) + '[INSTALLED - NOT SETUP]'.ljust(30))
            return True
        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
            return True
    elif pkg == "python":
        if float(platform.python_version()[:3]) < 2.7:
            print(pkg.ljust(20) + '[OUTDATED VERSION (<2.7)]'.ljust(30))
            return False
        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
            return True
    elif pkg == "cmake":
        CMAKE = os.environ.get('CMAKE', 'cmake')
        if not check_version_string_ge(exec_subprocess_check_output('{cmake} --version'.format(cmake=CMAKE), '/').strip().split('\n')[0].split()[-1], '3.4.3'):
            print(pkg.ljust(20) + '[OUTDATED VERSION (<3.4.3)]'.ljust(30))
            return False
        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
    elif pkg == "SSL":
        if sys.version_info < (3, 0):
            # Python 2.x
            import socket
            if hasattr(socket, 'ssl'):
                print(pkg.ljust(20) + '[SUPPORTED]'.ljust(30))
                return True
            else:
                print(pkg.ljust(20) + '[NOT SUPPORTED]'.ljust(30))
                return False
        else:
            # Python 3.x
            print(pkg.ljust(20) + '[SUPPORTED]'.ljust(30))
            return True

    elif exec_subprocess_check_output("dpkg-query -W -f='${Status}' %s 2>/dev/null | grep -c 'ok installed'" % (pkg),
                                      '/').strip() == '0':
        print(pkg.ljust(20) + '[NOT INSTALLED]'.ljust(30))
        return False
    else:
        if pkg == "gcc":
            if float(exec_subprocess_check_output('gcc -dumpversion', '/')[:3].strip()) <= 4.7:
                print(pkg.ljust(20) + '[UNSUPPORTED VERSION (<4.7)]'.ljust(30))
                return False
            else:
                print(pkg.ljust(20) + '[OK]'.ljust(30))
                return True
        elif pkg == "g++":
            if float(exec_subprocess_check_output('g++ -dumpversion', '/')[:3].strip()) <= 4.7:
                print(pkg.ljust(20) + '[UNSUPPORTED VERSION (<4.7)]'.ljust(30))
                return False
            else:
                print(pkg.ljust(20) + '[OK]'.ljust(30))
                return True
        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
            return True


def tarball_deb():
    box_draw("Compress compiled binaries into a bzip2 tarball")
    tar = tarfile.open(os.path.join(workdir, 'cling_' + VERSION + '.orig.tar.bz2'), 'w:bz2')
    tar.add(prefix, arcname=os.path.basename(prefix))
    tar.close()


def debianize():
    SIGNING_USER = exec_subprocess_check_output('gpg --fingerprint | grep uid | sed s/"uid *"//g',
                                                CLING_SRC_DIR).strip()

    box_draw("Set up the debian directory")
    print("Create directory: debian")
    os.makedirs(os.path.join(prefix, 'debian'))

    print("Create directory: " + os.path.join(prefix, 'debian', 'source'))
    os.makedirs(os.path.join(prefix, 'debian', 'source'))

    print("Create file: " + os.path.join(prefix, 'debian', 'source', 'format'))
    f = open(os.path.join(prefix, 'debian', 'source', 'format'), 'w')
    f.write('3.0 (quilt)')
    f.close()

    print("Create file: " + os.path.join(prefix, 'debian', 'source', 'lintian-overrides'))
    f = open(os.path.join(prefix, 'debian', 'source', 'lintian-overrides'), 'w')
    f.write('cling source: source-is-missing')
    f.close()

    # This section is no longer valid. I have kept it as a reference if we plan to
    # distribute libcling.so or any other library with the package.
    if False:
        print('Create file: ' + os.path.join(prefix, 'debian', 'postinst'))
        template = '''
#! /bin/sh -e
# postinst script for cling
#
# see: dh_installdeb(1)

set -e

# Call ldconfig on libclang.so
ldconfig -l /usr/lib/libclang.so

# dh_installdeb will replace this with shell code automatically
# generated by other debhelper scripts.

#DEBHELPER#

exit 0
'''
        f = open(os.path.join(prefix, 'debian', 'postinst'), 'w')
        f.write(template)
        f.close()

    print('Create file: ' + os.path.join(prefix, 'debian', 'cling.install'))
    f = open(os.path.join(prefix, 'debian', 'cling.install'), 'w')
    template = '''
bin/* /usr/bin
docs/* /usr/share/doc
include/* /usr/include
lib/* /usr/lib
share/* /usr/share
'''
    f.write(template.strip())
    f.close()

    print('Create file: ' + os.path.join(prefix, 'debian', 'compact'))
    # Optimize binary compression
    f = open(os.path.join(prefix, 'debian', 'compact'), 'w')
    f.write("7")
    f.close()

    print('Create file: ' + os.path.join(prefix, 'debian', 'compat'))
    f = open(os.path.join(prefix, 'debian', 'compat'), 'w')
    f.write("9")
    f.close()

    print('Create file: ' + os.path.join(prefix, 'debian', 'control'))
    f = open(os.path.join(prefix, 'debian', 'control'), 'w')
    template = '''
Source: cling
Section: devel
Priority: optional
Maintainer: Cling Developer Team <cling-dev@cern.ch>
Uploaders: %s
Build-Depends: debhelper (>= 9.0.0)
Standards-Version: 3.9.5
Homepage: http://cling.web.cern.ch/
Vcs-Git: http://root.cern.ch/git/cling.git
Vcs-Browser: http://root.cern.ch/gitweb?p=cling.git;a=summary

Package: cling
Priority: optional
Architecture: any
Depends: ${shlibs:Depends}, ${misc:Depends}
Description: interactive C++ interpreter
 Cling is a new and interactive C++11 standard compliant interpreter built
 on the top of Clang and LLVM compiler infrastructure. Its advantages over
 the standard interpreters are that it has command line prompt and uses
 Just In Time (JIT) compiler for compilation. Many of the developers
 (e.g. Mono in their project called CSharpRepl) of such kind of software
 applications name them interactive compilers.
 .
 One of Cling's main goals is to provide contemporary, high-performance
 alternative of the current C++ interpreter in the ROOT project - CINT. Cling
 serves as a core component of the ROOT system for storing and analyzing the
 data of the Large Hadron Collider (LHC) experiments. The
 backward-compatibility with CINT is major priority during the development.
''' % (SIGNING_USER)
    f.write(template.strip())
    f.close()

    print('Create file: ' + os.path.join(prefix, 'debian', 'copyright'))
    f = open(os.path.join(prefix, 'debian', 'copyright'), 'w')
    template = '''
Format: http://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: cling
Source: http://root.cern.ch/gitweb?p=cling.git;a=summary

Files: *
Copyright: 2007-2014 by the Authors
License: LGPL-2.0+
Comment: Developed by The ROOT Team; CERN and Fermilab

Files: debian/*
Copyright: 2014 Anirudha Bose <ani07nov@gmail.com>
License: LGPL-2.0+

License: LGPL-2.0+
 This package is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2 of the License, or (at your option) any later version.
 .
 This package is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 .
 You should have received a copy of the GNU General Public License
 along with this program. If not, see <http://www.gnu.org/licenses/>.
 .
 On Debian systems, the complete text of the GNU Lesser General
 Public License can be found in "/usr/share/common-licenses/LGPL-2".
Comment: Cling can also be licensed under University of Illinois/NCSA
 Open Source License (UI/NCSAOSL).
 .
 More information here: http://root.cern.ch/gitweb?p=cling.git;a=blob_plain;f=LICENSE.TXT;hb=HEAD
'''
    f.write(template.strip())
    f.close()

    print('Create file: ' + os.path.join(prefix, 'debian', 'rules'))
    f = open(os.path.join(prefix, 'debian', 'rules'), 'w')
    template = '''
#!/usr/bin/make -f
# -*- makefile -*-

%:
\tdh $@

override_dh_auto_build:

override_dh_auto_install:
'''
    f.write(template.strip())
    f.close()

    print('Create file: ' + os.path.join(prefix, 'debian', 'changelog'))
    f = open(os.path.join(prefix, 'debian', 'changelog'), 'w')

    template = '''
cling (%s-1) unstable; urgency=low

  * [Debian] Upload to unstable for version: %s
''' % (VERSION, VERSION)
    f.write(template.lstrip())
    f.close()

    STABLE_FLAG = '0'
    if '~dev' in VERSION:
        TAG = str(float(VERSION[:VERSION.find('~')]) - 0.1)
        template = exec_subprocess_check_output('git log v' + TAG + '...HEAD --format="  * %s" | fmt -s', CLING_SRC_DIR)

        f = open(os.path.join(prefix, 'debian', 'changelog'), 'a+')
        f.write(template)
        f.close()

        f = open(os.path.join(prefix, 'debian', 'changelog'), 'a+')
        f.write('\n -- ' + SIGNING_USER + '  ' + formatdate(time.time(), tzinfo()) + '\n')
        f.close()
    else:
        TAG = VERSION.replace('v', '')
        if TAG == '0.1':
            f = open(os.path.join(prefix, 'debian', 'changelog'), 'a+')
            f.write('\n -- ' + SIGNING_USER + '  ' + formatdate(time.time(), tzinfo()) + '\n')
            f.close()
        STABLE_FLAG = '1'

    while TAG != '0.1':
        CMP = TAG
        TAG = str(float(TAG) - 0.1)
        if STABLE_FLAG != '1':
            f = open(os.path.join(prefix, 'debian', 'changelog'), 'a+')
            f.write('cling (' + TAG + '-1) unstable; urgency=low\n')
            f.close()
            STABLE_FLAG = '1'
            template = exec_subprocess_check_output('git log v' + CMP + '...v' + TAG + '--format="  * %s" | fmt -s',
                                                    CLING_SRC_DIR)

            f = open(os.path.join(prefix, 'debian', 'changelog'), 'a+')
            f.write(template)
            f.close()

            f = open(os.path.join(prefix, 'debian', 'changelog'), 'a+')
            f.write('\n -- ' + SIGNING_USER + '  ' + formatdate(time.time(), tzinfo()) + '\n')
            f.close()

    # Changelog entries from first commit to v0.1
    f = open(os.path.join(prefix, 'debian', 'changelog'), 'a+')
    f.write('\nOld Changelog:\n')
    f.close()

    template = exec_subprocess_check_output('git log v0.1 --format="  * %s%n -- %an <%ae>  %cD%n"', CLING_SRC_DIR)

    f = open(os.path.join(prefix, 'debian', 'changelog'), 'a+')
    f.write(template.encode('utf-8'))
    f.close()

    box_draw("Run debuild to create Debian package")
    exec_subprocess_call('debuild', prefix)


###############################################################################
#                          Red Hat specific functions                         #
###############################################################################

def check_redhat(pkg):
    if pkg == "python":
        if platform.python_version()[0] == '3':
            print(pkg.ljust(20) + '[UNSUPPORTED VERSION (Python 3)]'.ljust(30))
            return False
        elif float(platform.python_version()[:3]) < 2.7:
            print(pkg.ljust(20) + '[OUTDATED VERSION (<2.7)]'.ljust(30))
            return False
        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
            return True
    elif pkg == "cmake":
        CMAKE = os.environ.get('CMAKE', 'cmake')
        if not check_version_string_ge(exec_subprocess_check_output('{cmake} --version'.format(cmake=CMAKE), '/').strip().split('\n')[0].split()[-1], '3.4.3'):
            print(pkg.ljust(20) + '[OUTDATED VERSION (<3.4.3)]'.ljust(30))
            return False
        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
    elif pkg == "SSL":
        if sys.version_info < (3, 0):
            # Python 2.x
            import socket
            if hasattr(socket, 'ssl'):
                print(pkg.ljust(20) + '[SUPPORTED]'.ljust(30))
                return True
            else:
                print(pkg.ljust(20) + '[NOT SUPPORTED]'.ljust(30))
                return False
        else:
            # Python 3.x
            print(pkg.ljust(20) + '[SUPPORTED]'.ljust(30))
            return True

    elif exec_subprocess_check_output("rpm -qa | grep -w %s" % (pkg), '/').strip() == '':
        print(pkg.ljust(20) + '[NOT INSTALLED]'.ljust(30))
        return False
    else:
        if pkg == "gcc-c++":
            if float(exec_subprocess_check_output('g++ -dumpversion', '/')[:3].strip()) <= 4.7:
                print(pkg.ljust(20) + '[UNSUPPORTED VERSION (<4.7)]'.ljust(30))
                return False
            else:
                print(pkg.ljust(20) + '[OK]'.ljust(30))
                return True
        elif pkg == "gcc":
            if float(exec_subprocess_check_output('gcc -dumpversion', '/')[:3].strip()) <= 4.7:
                print(pkg.ljust(20) + '[UNSUPPORTED VERSION (<4.7)]'.ljust(30))
                return False
            else:
                print(pkg.ljust(20) + '[OK]'.ljust(30))
                return True

        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
            return True


def rpm_build():
    global REVISION
    box_draw("Set up RPM build environment")
    if os.path.isdir(os.path.join(workdir, 'rpmbuild')):
        shutil.rmtree(os.path.join(workdir, 'rpmbuild'))
    os.makedirs(os.path.join(workdir, 'rpmbuild'))
    os.makedirs(os.path.join(workdir, 'rpmbuild', 'RPMS'))
    os.makedirs(os.path.join(workdir, 'rpmbuild', 'BUILD'))
    os.makedirs(os.path.join(workdir, 'rpmbuild', 'SOURCES'))
    os.makedirs(os.path.join(workdir, 'rpmbuild', 'SPECS'))
    os.makedirs(os.path.join(workdir, 'rpmbuild', 'tmp'))
    shutil.move(os.path.join(workdir, os.path.basename(prefix) + '.tar.bz2'),
                os.path.join(workdir, 'rpmbuild', 'SOURCES'))

    box_draw("Generate RPM SPEC file")
    print('Create file: ' + os.path.join(workdir, 'rpmbuild', 'SPECS', 'cling-%s.spec' % (VERSION)))
    f = open(os.path.join(workdir, 'rpmbuild', 'SPECS', 'cling-%s.spec' % (VERSION)), 'w')

    if REVISION == '':
        REVISION = '1'

    template = '''
%define        __spec_install_post %{nil}
%define          debug_package %{nil}
%define        __os_install_post %{_dbpath}/brp-compress

Summary: Interactive C++ interpreter
Name: cling
Version: 0.2~dev
Release: ''' + REVISION[:7] + '''
License: LGPLv2+ or NCSA
Group: Development/Languages/Other
SOURCE0 : %{name}-%{version}.tar.bz2
URL: http://cling.web.cern.ch/
Vendor: Developed by The ROOT Team; CERN and Fermilab
Packager: Anirudha Bose <ani07nov@gmail.com>

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
Cling is a new and interactive C++11 standard compliant interpreter built
on the top of Clang and LLVM compiler infrastructure. Its advantages over
the standard interpreters are that it has command line prompt and uses
Just In Time (JIT) compiler for compilation. Many of the developers
(e.g. Mono in their project called CSharpRepl) of such kind of software
applications name them interactive compilers.

One of Cling's main goals is to provide contemporary, high-performance
alternative of the current C++ interpreter in the ROOT project - CINT. Cling
serves as a core component of the ROOT system for storing and analyzing the
data of the Large Hadron Collider (LHC) experiments. The
backward-compatibility with CINT is major priority during the development.

%prep
%setup
mkdir -p $RPM_BUILD_DIR/%{name}-%{version}/usr/share/doc
mv $RPM_BUILD_DIR/%{name}-%{version}/bin/ $RPM_BUILD_DIR/%{name}-%{version}/usr
mv $RPM_BUILD_DIR/%{name}-%{version}/docs/* $RPM_BUILD_DIR/%{name}-%{version}/usr/share/doc/
mv $RPM_BUILD_DIR/%{name}-%{version}/lib/ $RPM_BUILD_DIR/%{name}-%{version}/usr
mv $RPM_BUILD_DIR/%{name}-%{version}/include/ $RPM_BUILD_DIR/%{name}-%{version}/usr
mv $RPM_BUILD_DIR/%{name}-%{version}/share/* $RPM_BUILD_DIR/%{name}-%{version}/usr/share

rm -Rf $RPM_BUILD_DIR/%{name}-%{version}/docs
rm -Rf $RPM_BUILD_DIR/%{name}-%{version}/share

if [ ${RPM_ARCH} = 'x86_64' ]; then
    mv $RPM_BUILD_DIR/%{name}-%{version}/usr/lib $RPM_BUILD_DIR/%{name}-%{version}/usr/lib64
fi

%build
# Empty section.

%install
rm -rf %{buildroot}
mkdir -p  %{buildroot}

# in builddir
cp -a * %{buildroot}


%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_bindir}/*
%{_includedir}/*
%{_libdir}/*
%{_datadir}/*

%changelog
* Sun Apr 13 2014  Anirudha Bose <ani07nov@gmail.com>
- Initial SPEC file of Cling for RPM packaging
'''
    f.write(template.strip())
    f.close()

    box_draw('Run rpmbuild program')
    exec_subprocess_call('rpmbuild --define "_topdir ${PWD}" -bb %s' % (
        os.path.join(workdir, 'rpmbuild', 'SPECS', 'cling-%s.spec' % (VERSION))), os.path.join(workdir, 'rpmbuild'))


###############################################################################
#           Windows specific functions (ported from windows_dep.sh)           #
###############################################################################

def check_win(pkg):
    # Check for Microsoft Visual Studio 14.0
    if pkg == "msvc":
        if exec_subprocess_check_output('REG QUERY HKEY_CLASSES_ROOT\\VisualStudio.DTE.14.0', 'C:\\').find(
                'ERROR') == -1:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
        else:
            print(pkg.ljust(20) + '[NOT INSTALLED]'.ljust(30))

    elif pkg == "python":
        if platform.python_version()[0] == '3':
            print(pkg.ljust(20) + '[UNSUPPORTED VERSION (Python 3)]'.ljust(30))
        elif float(platform.python_version()[:3]) < 2.7:
            print(pkg.ljust(20) + '[OUTDATED VERSION (<2.7)]'.ljust(30))
        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
    elif pkg == 'SSL':
        if sys.version_info < (3, 0):
            # Python 2.x
            import socket
            if hasattr(socket, 'ssl'):
                print(pkg.ljust(20) + '[SUPPORTED]'.ljust(30))
            else:
                print(pkg.ljust(20) + '[NOT SUPPORTED]'.ljust(30))
        else:
            # Python 3.x
            print(pkg.ljust(20) + '[SUPPORTED]'.ljust(30))

            # Check for other tools
    else:
        if exec_subprocess_check_output('where %s' % (pkg), 'C:\\').find(
                'INFO: Could not find files for the given pattern') != -1:
            print(pkg.ljust(20) + '[NOT INSTALLED]'.ljust(30))
        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))


def is_os_64bit():
    return platform.machine().endswith('64')

def get_win_dep():

    if args['current_dev'] == 'nsis' or (args['current_dev'] == 'pkg' and OS == 'Windows'):
        box_draw("Download NSIS compiler")
        html = urlopen('https://sourceforge.net/p/nsis/code/6780/log/?path=/NSIS/tags').read().decode('utf-8')
        pin = '<p>Tagging for release'
        NSIS_VERSION = html[html.find(pin):html.find('</div>', html.find(pin))].strip(pin + ' ')
        print('Latest version of NSIS is: ' + NSIS_VERSION)
        wget(url="https://sourceforge.net/projects/nsis/files/NSIS%%203/%s/nsis-%s.zip" % (
            NSIS_VERSION, NSIS_VERSION),
             out_dir=TMP_PREFIX)
        print('Extracting: ' + os.path.join(TMP_PREFIX, 'nsis-%s.zip' % (NSIS_VERSION)))
        zip = zipfile.ZipFile(os.path.join(TMP_PREFIX, 'nsis-%s.zip' % (NSIS_VERSION)))
        zip.extractall(os.path.join(TMP_PREFIX, 'bin'))
        print('Remove file: ' + os.path.join(TMP_PREFIX, 'nsis-%s.zip' % (NSIS_VERSION)))
        os.rename(os.path.join(TMP_PREFIX, 'bin', 'nsis-%s' % (NSIS_VERSION)), os.path.join(TMP_PREFIX, 'bin', 'nsis'))

    def tryCmake(cmake):
        try:
            rslt = exec_subprocess_check_output(cmake + ' --version', TMP_PREFIX)
            vers = [int(v) for v in rslt.split()[2].split('.')]
            if vers[0] >= 3 and (vers[1] > 6 or (vers[1]==6 and vers[2] >= 2)):
                return cmake
        except:
            pass
        return False

    global CMAKE
    cmakeEXE = tryCmake('cmake.exe') or tryCmake(CMAKE)
    if cmakeEXE:
        CMAKE = cmakeEXE
        box_draw("Using previous CMake: %s" % cmakeEXE)
        return

    box_draw("Download CMake v3.6.2 required for Windows")
    if is_os_64bit():
        wget(url='https://cmake.org/files/v3.6/cmake-3.6.2-win64-x64.zip',
             out_dir=TMP_PREFIX, rename_file='cmake-3.6.2.zip')
    else:
        wget(url='https://cmake.org/files/v3.6/cmake-3.6.2-win32-x86.zip',
             out_dir=TMP_PREFIX, rename_file='cmake-3.6.2.zip')

    zip_file = os.path.join(TMP_PREFIX, 'cmake-3.6.2.zip')
    print('Extracting: ' + zip_file)
    zip = zipfile.ZipFile(zip_file)
    tmp_bin_dir = os.path.join(TMP_PREFIX, 'bin')
    zip.extractall(tmp_bin_dir)
    print('Remove file: ' + os.path.join(TMP_PREFIX, 'cmake-3.6.2.zip'))

    if is_os_64bit():
        os.rename(os.path.join(tmp_bin_dir, 'cmake-3.6.2-win64-x64'), cmakeDir)
    else:
        os.rename(os.path.join(tmp_bin_dir, 'cmake-3.6.2-win32-x86'), cmakeDir)
    print()


def make_nsi():
    box_draw("Generating cling.nsi")
    NSIS = os.path.join(TMP_PREFIX, 'bin', 'nsis')
    VIProductVersion = \
        exec_subprocess_check_output('git describe --match v* --abbrev=0 --tags', CLING_SRC_DIR).strip().splitlines()[0]
    print('Create file: ' + os.path.join(workdir, 'cling.nsi'))
    f = open(os.path.join(workdir, 'cling.nsi'), 'w')
    template = r'''
; Cling setup script %s
!define APP_NAME "Cling"
!define COMP_NAME "CERN"
!define WEB_SITE "http://cling.web.cern.ch/"
!define VERSION "%s"
!define COPYRIGHT "Copyright © 2007-2014 by the Authors; Developed by The ROOT Team, CERN and Fermilab"
!define DESCRIPTION "Interactive C++ interpreter"
!define INSTALLER_NAME "%s"
!define MAIN_APP_EXE "cling.exe"
!define INSTALL_TYPE "SetShellVarContext current"
!define PRODUCT_ROOT_KEY "HKLM"
!define PRODUCT_KEY "Software\Cling"

###############################################################################

VIProductVersion  "%s.0.0"
VIAddVersionKey "ProductName"  "${APP_NAME}"
VIAddVersionKey "CompanyName"  "${COMP_NAME}"
VIAddVersionKey "LegalCopyright"  "${COPYRIGHT}"
VIAddVersionKey "FileDescription"  "${DESCRIPTION}"
VIAddVersionKey "FileVersion"  "${VERSION}"

###############################################################################

SetCompressor /SOLID Lzma
Name "${APP_NAME}"
Caption "${APP_NAME}"
OutFile "${INSTALLER_NAME}"
BrandingText "${APP_NAME}"
XPStyle on
InstallDir "C:\Cling\cling-${VERSION}"

###############################################################################
; MUI settings
!include "MUI.nsh"

!define MUI_ABORTWARNING
!define MUI_UNABORTWARNING
!define MUI_HEADERIMAGE

; Theme
!define MUI_ICON "%s\LLVM.ico"
!define MUI_UNICON "%s\Contrib\Graphics\Icons\orange-uninstall.ico"

!insertmacro MUI_PAGE_WELCOME

!define MUI_LICENSEPAGE_TEXT_BOTTOM "The source code for Cling is freely redistributable under the terms of the GNU Lesser General Public License (LGPL) as published by the Free Software Foundation."
!define MUI_LICENSEPAGE_BUTTON "Next >"
!insertmacro MUI_PAGE_LICENSE "%s"

!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\${MAIN_APP_EXE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM

!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

###############################################################################

Function .onInit
  Call DetectWinVer
  Call CheckPrevVersion
FunctionEnd

; file section
Section "MainFiles"
''' % (prefix,
       VERSION,
       os.path.basename(prefix) + '-setup.exe',
       VIProductVersion.replace('v', ''),
       CPT_SRC_DIR,
       NSIS,
       os.path.join(CLING_SRC_DIR, 'LICENSE.TXT'))

    f.write(template.lstrip())
    f.close()

    # Insert the files to be installed
    f = open(os.path.join(workdir, 'cling.nsi'), 'a+')
    for root, dirs, files in os.walk(prefix):
        f.write(' CreateDirectory "$INSTDIR\\%s"\n' % (root.replace(prefix, '')))
        f.write(' SetOutPath "$INSTDIR\\%s"\n' % (root.replace(prefix, '')))

        for file in files:
            path = os.path.join(root, file)
            f.write(' File "%s"\n' % (path))

    template = r'''
SectionEnd

Section make_uninstaller
 ; Write the uninstall keys for Windows
 SetOutPath "$INSTDIR"
 WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Cling" "DisplayName" "Cling"
 WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Cling" "UninstallString" "$INSTDIR\uninstall.exe"
 WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Cling" "NoModify" 1
 WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Cling" "NoRepair" 1
 WriteUninstaller "uninstall.exe"
SectionEnd

; start menu
# TODO: This is currently hardcoded.
Section "Shortcuts"

 CreateDirectory "$SMPROGRAMS\Cling"
 CreateShortCut "$SMPROGRAMS\Cling\Uninstall.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
 CreateShortCut "$SMPROGRAMS\Cling\Cling.lnk" "$INSTDIR\bin\cling.exe" "" "${MUI_ICON}" 0
 CreateDirectory "$SMPROGRAMS\Cling\Documentation"
 CreateShortCut "$SMPROGRAMS\Cling\Documentation\Cling (PS).lnk" "$INSTDIR\docs\llvm\ps\cling.ps" "" "" 0
 CreateShortCut "$SMPROGRAMS\Cling\Documentation\Cling (HTML).lnk" "$INSTDIR\docs\llvm\html\cling\cling.html" "" "" 0

SectionEnd

Section "Uninstall"

 DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Cling"
 DeleteRegKey HKLM "Software\Cling"

 ; Remove shortcuts
 Delete "$SMPROGRAMS\Cling\*.*"
 Delete "$SMPROGRAMS\Cling\Documentation\*.*"
 Delete "$SMPROGRAMS\Cling\Documentation"
 RMDir "$SMPROGRAMS\Cling"

'''
    f.write(template)

    # insert dir list (depth-first order) for uninstall files
    def walktree(top=prefix):
        names = os.listdir(top)
        for name in names:
            try:
                st = os.lstat(os.path.join(top, name))
            except os.error:
                continue
            if stat.S_ISDIR(st.st_mode):
                for (newtop, children) in walktree(os.path.join(top, name)):
                    yield newtop, children
        yield top, names

    def iterate():
        for (basepath, children) in walktree():
            f.write(' Delete "%s\\*.*"\n' % (basepath.replace(prefix, '$INSTDIR')))
            f.write(' RmDir "%s"\n' % (basepath.replace(prefix, '$INSTDIR')))

    iterate()

    # last bit of the uninstaller
    template = r'''
SectionEnd

; Function to detect Windows version and abort if Cling is unsupported in the current platform
Function DetectWinVer
  Push $0
  Push $1
  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" CurrentVersion
  IfErrors is_error is_winnt
is_winnt:
  StrCpy $1 $0 1
  StrCmp $1 4 is_error ; Aborting installation for Windows versions older than Windows 2000
  StrCmp $0 "5.0" is_error ; Removing Windows 2000 as supported Windows version
  StrCmp $0 "5.1" is_winnt_XP
  StrCmp $0 "5.2" is_winnt_2003
  StrCmp $0 "6.0" is_winnt_vista
  StrCmp $0 "6.1" is_winnt_7
  StrCmp $0 "6.2" is_winnt_8
  StrCmp $1 6 is_winnt_8 ; Checking for future versions of Windows 8
  Goto is_error

is_winnt_XP:
is_winnt_2003:
is_winnt_vista:
is_winnt_7:
is_winnt_8:
  Goto done
is_error:
  StrCpy $1 $0
  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" ProductName
  IfErrors 0 +4
  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion" Version
  IfErrors 0 +2
  StrCpy $0 "Unknown"
  MessageBox MB_ICONSTOP|MB_OK "This version of Cling cannot be installed on this system. Cling is supported only on Windows NT systems. Current system: $0 (version: $1)"
  Abort
done:
  Pop $1
  Pop $0
FunctionEnd

; Function to check any previously installed version of Cling in the system
Function CheckPrevVersion
  Push $0
  Push $1
  Push $2
  IfFileExists "$INSTDIR\bin\cling.exe" 0 otherver
  MessageBox MB_OK|MB_ICONSTOP "Another Cling installation (with the same version) has been detected. Please uninstall it first."
  Abort
otherver:
  StrCpy $0 0
  StrCpy $2 ""
loop:
  EnumRegKey $1 ${PRODUCT_ROOT_KEY} "${PRODUCT_KEY}" $0
  StrCmp $1 "" loopend
  IntOp $0 $0 + 1
  StrCmp $2 "" 0 +2
  StrCpy $2 "$1"
  StrCpy $2 "$2, $1"
  Goto loop
loopend:
  ReadRegStr $1 ${PRODUCT_ROOT_KEY} "${PRODUCT_KEY}" "Version"
  IfErrors finalcheck
  StrCmp $2 "" 0 +2
  StrCpy $2 "$1"
  StrCpy $2 "$2, $1"
finalcheck:
  StrCmp $2 "" done
  MessageBox MB_YESNO|MB_ICONEXCLAMATION "Another Cling installation (version $2) has been detected. It is recommended to uninstall it if you intend to use the same installation directory. Do you want to proceed with the installation anyway?" IDYES done IDNO 0
  Abort
done:
  ClearErrors
  Pop $2
  Pop $1
  Pop $0
FunctionEnd
'''
    f.write(template)
    f.close()


def build_nsis():
    box_draw("Build NSIS executable from cling.nsi")
    NSIS = os.path.join(TMP_PREFIX, 'bin', 'nsis')
    exec_subprocess_call('%s -V3 %s' % (os.path.join(NSIS, 'makensis.exe'), os.path.join(workdir, 'cling.nsi')),
                         workdir)


###############################################################################
#                          Mac OS X specific functions                        #
###############################################################################

def check_mac(pkg):
    if pkg == "python":
        if platform.python_version()[0] == '3':
            print(pkg.ljust(20) + '[UNSUPPORTED VERSION (Python 3)]'.ljust(30))
            return False
        elif float(platform.python_version()[:3]) < 2.7:
            print(pkg.ljust(20) + '[OUTDATED VERSION (<2.7)]'.ljust(30))
            return False
        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
            return True
    elif pkg == "cmake":
        CMAKE = os.environ.get('CMAKE', 'cmake')
        if not check_version_string_ge(exec_subprocess_check_output('{cmake} --version'.format(cmake=CMAKE), '/').strip().split('\n')[0].split()[-1].split('-')[0], '3.4.3'):
            print(pkg.ljust(20) + '[OUTDATED VERSION (<3.4.3)]'.ljust(30))
            return False
        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
    elif pkg == "SSL":
        if sys.version_info < (3, 0):
            # Python 2.x
            import socket
            if hasattr(socket, 'ssl'):
                print(pkg.ljust(20) + '[SUPPORTED]'.ljust(30))
                return True
            else:
                print(pkg.ljust(20) + '[NOT SUPPORTED]'.ljust(30))
                return False
        else:
            # Python 3.x
            print(pkg.ljust(20) + '[SUPPORTED]'.ljust(30))
            return True

    elif exec_subprocess_check_output("type -p %s" % (pkg), '/').strip() == '':
        print(pkg.ljust(20) + '[NOT INSTALLED]'.ljust(30))
        return False
    else:
        if pkg == "clang++":
            if float(exec_subprocess_check_output('clang++ -dumpversion', '/')[:3].strip()) <= 4.1:
                print(pkg.ljust(20) + '[UNSUPPORTED VERSION (<4.1)]'.ljust(30))
                return False
            else:
                print(pkg.ljust(20) + '[OK]'.ljust(30))
                return True
        elif pkg == "clang":
            if float(exec_subprocess_check_output('clang -dumpversion', '/')[:3].strip()) <= 4.1:
                print(pkg.ljust(20) + '[UNSUPPORTED VERSION (<4.1)]'.ljust(30))
                return False
            else:
                print(pkg.ljust(20) + '[OK]'.ljust(30))
                return True
        else:
            print(pkg.ljust(20) + '[OK]'.ljust(30))
            return True


def make_dmg():
    box_draw("Building Apple Disk Image")
    APP_NAME = 'Cling'
    # DMG_BACKGROUND_IMG = 'graphic.png'  # TODO: use
    APP_EXE = '%s.app/Contents/MacOS/bin/%s' % (APP_NAME, APP_NAME.lower())
    VOL_NAME = "%s-%s" % (APP_NAME.lower(), VERSION)
    DMG_TMP = "%s-temp.dmg" % (VOL_NAME)
    DMG_FINAL = "%s.dmg" % (VOL_NAME)
    STAGING_DIR = os.path.join(workdir, 'Install')

    pip_install('pyobjc-core')
    pip_install('dmgbuild')

    if os.path.isdir(STAGING_DIR):
        print("Remove directory: " + STAGING_DIR)
        shutil.rmtree(STAGING_DIR)

    if os.path.isdir(os.path.join(workdir, '%s.app' % (APP_NAME))):
        print("Remove directory: " + os.path.join(workdir, '%s.app' % (APP_NAME)))
        shutil.rmtree(os.path.join(workdir, '%s.app' % (APP_NAME)))

    if os.path.isfile(os.path.join(workdir, DMG_TMP)):
        print("Remove file: " + os.path.join(workdir, DMG_TMP))
        os.remove(os.path.join(workdir, DMG_TMP))

    if os.path.isfile(os.path.join(workdir, DMG_FINAL)):
        print("Remove file: " + os.path.join(workdir, DMG_FINAL))
        os.remove(os.path.join(workdir, DMG_FINAL))


    if os.path.isdir(os.path.join(workdir, '%s.app' % (APP_NAME))):
        print("Remove directory:", os.path.join(workdir, '%s.app' % (APP_NAME)))
        shutil.rmtree(os.path.join(workdir, '%s.app' % (APP_NAME)))

    print('Create directory: ' + os.path.join(workdir, '%s.app' % (APP_NAME)))
    os.makedirs(os.path.join(workdir, '%s.app' % (APP_NAME)))

    print('Populate directory: ' + os.path.join(workdir, '%s.app' % (APP_NAME), 'Contents', 'MacOS'))
    shutil.copytree(
        prefix,
        os.path.join(workdir, '%s.app'%(APP_NAME), 'Contents', 'MacOS')
    )

    os.makedirs(os.path.join(workdir, '%s.app'% (APP_NAME), 'Contents', 'Resources'))
    shutil.copyfile(
        os.path.join(CPT_SRC_DIR, 'LLVM.icns'),
        os.path.join(workdir, '%s.app'% (APP_NAME), 'Contents', 'Resources', 'LLVM.icns')
    )


    print('Configuring Info.plist file')
    plist_path = os.path.join(workdir, '%s.app'% (APP_NAME), 'Contents', 'Info.plist')
    f = open(plist_path, 'w')
    plist_xml = '''
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleGetInfoString</key>
  <string>Copyright © 2007-2014 by the Authors; Developed by The ROOT Team, CERN and Fermilab</string>
  <key>CFBundleExecutable</key>
  <string>bin/cling</string>
  <key>CFBundleIdentifier</key>
  <string>ch.cern.root.cling</string>
  <key>CFBundleName</key>
  <string>Cling</string>
  <key>CFBundleIconFile</key>
  <string>LLVM</string>
  <key>CFBundleShortVersionString</key>
  <string>{version}</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleSignature</key>
  <string>llvm</string>
  <key>IFMajorVersion</key>
  <integer>{major}</integer>
  <key>IFMinorVersion</key>
  <integer>{minor}</integer>

</dict>
</plist>
'''.format(
        version=VERSION,
        major=VERSION.split('.')[0],
        minor=VERSION.split('.')[1]
    ).strip()

    f.write(plist_xml)
    f.close()


    print('Copy APP Bundle to staging area: ' + STAGING_DIR)
    shutil.copytree(os.path.join(workdir, '%s.app' % (APP_NAME)), STAGING_DIR)

    print('Stripping file: ' + APP_EXE)
    exec_subprocess_call('strip -u -r %s' % os.path.join(workdir, APP_EXE), workdir)

    DU = exec_subprocess_check_output("du -sh %s" % (STAGING_DIR), workdir)
    SIZE = str(float(DU[:DU.find('M')].strip()) + 1.0)
    print('Estimated size of application bundle: ' + SIZE + 'MB')

    exec_subprocess_call(
        '{dmgbuild} -s {settings} -D app={app} -D size={size}M "{volname}" {dmg}'.format(
            dmgbuild=os.path.join(workdir, 'pip', 'bin', 'dmgbuild'),
            settings=os.path.join(CPT_SRC_DIR, 'settings.py'),
            app=os.path.join(workdir, '%s.app' % (APP_NAME)),
            dmg=DMG_FINAL,
            volname=VOL_NAME,
            size=SIZE
        ),
        workdir
    )

    print('Syncing disk')
    exec_subprocess_call(
        'sync',
        workdir
    )

    print('Done')


###############################################################################
#                           argparse configuration                            #
###############################################################################

parser = argparse.ArgumentParser(description='Cling Packaging Tool')
parser.add_argument('-c', '--check-requirements', help='Check if packages required by the script are installed',
                    action='store_true')
parser.add_argument('--current-dev',
                    help=('--current-dev:<tar | deb | nsis | rpm | dmg | pkg> will package the latest development snapshot in the given format'
                          + '\n--current-dev:branch:<branch> will build <branch> on llvm, clang, and cling'
                          + '\n--current-dev:branches:<a,b,c> will build branch <a> on llvm, <b> on clang, and <c> on cling'))
parser.add_argument('--last-stable',
                    help='Package the last stable snapshot in one of these formats: tar | deb | nsis | rpm | dmg | pkg')
parser.add_argument('--tarball-tag', help='Package the snapshot of a given tag in a tarball (.tar.bz2)')
parser.add_argument('--deb-tag', help='Package the snapshot of a given tag in a Debian package (.deb)')
parser.add_argument('--rpm-tag', help='Package the snapshot of a given tag in an RPM package (.rpm)')
parser.add_argument('--nsis-tag', help='Package the snapshot of a given tag in an NSIS installer (.exe)')
parser.add_argument('--dmg-tag', help='Package the snapshot of a given tag in a DMG package (.dmg)')

# Variable overrides
parser.add_argument('--with-llvm-url', action='store', help='Specify an alternate URL of LLVM repo',
                    default='http://root.cern.ch/git/llvm.git')
parser.add_argument('--with-clang-url', action='store', help='Specify an alternate URL of Clang repo',
                    default='http://root.cern.ch/git/clang.git')
parser.add_argument('--with-cling-url', action='store', help='Specify an alternate URL of Cling repo',
                    default='https://github.com/root-project/cling.git')

parser.add_argument('--no-test', help='Do not run test suite of Cling', action='store_true')
parser.add_argument('--skip-cleanup', help='Do not clean up after a build', action='store_true')
parser.add_argument('--use-wget', help='Do not use Git to fetch sources', action='store_true')
parser.add_argument('--create-dev-env', help='Set up a release/debug environment')

if platform.system() != 'Windows':
    parser.add_argument('--with-workdir', action='store', help='Specify an alternate working directory for CPT',
                        default=os.path.expanduser(os.path.join('~', 'ci', 'build')))
else:
    parser.add_argument('--with-workdir', action='store', help='Specify an alternate working directory for CPT',
                        default='C:\\ci\\build\\')

parser.add_argument('--make-proper', help='Internal option to support calls from build system')
parser.add_argument('--verbose', help='Tell CMake to build with verbosity', action='store_true')
parser.add_argument('--with-cmake-flags', help='Additional CMake configuration flags', default='')
parser.add_argument('--stdlib', help=('C++ Library to use, stdlibc++ or libc++.'
                                     '  To build a spcific llvm <tag> of libc++ with cling '
                                     'specify libc++,<tag>'),
                    default='')
parser.add_argument('--compiler', help='The compiler being used to make cling (for heuristics only)',
                    default='')
parser.add_argument('-y', help='Non-interactive mode (yes to all)', action='store_true')

args = vars(parser.parse_args())

###############################################################################
#                           Customized input                                  #
###############################################################################

def custom_input(prompt, always_yes=False):
    if always_yes:
        return 'y'
    else:
        return input(prompt)

###############################################################################
#                           Platform initialization                           #
###############################################################################

OS = platform.system()
FAMILY = os.name.upper()

if OS == 'Windows':
    DIST = 'N/A'
    RELEASE = OS + ' ' + platform.release()
    REV = platform.version()

    EXEEXT = '.exe'
    SHLIBEXT = '.dll'

    TMP_PREFIX = 'C:\\Windows\\Temp\\cling-obj\\'

elif OS == 'Linux':
    DIST = platform.linux_distribution()[0]
    RELEASE = platform.linux_distribution()[2]
    REV = platform.linux_distribution()[1]

    EXEEXT = ''
    SHLIBEXT = '.so'

    TMP_PREFIX = os.path.join(os.sep, 'tmp', 'cling-obj' + os.sep)

elif OS == 'Darwin':
    DIST = 'MacOSX'
    RELEASE = platform.release()
    REV = platform.mac_ver()[0]

    EXEEXT = ''
    SHLIBEXT = '.dylib'

    TMP_PREFIX = os.path.join(os.sep, 'tmp', 'cling-obj' + os.sep)

else:
    # Extensions will be detected anyway by set_ext()
    EXEEXT = ''
    SHLIBEXT = ''

    # TODO: Need to test this in other platforms
    TMP_PREFIX = os.path.join(os.sep, 'tmp', 'cling-obj' + os.sep)

###############################################################################
#                               Global variables                              #
###############################################################################

workdir = os.path.abspath(os.path.expanduser(args['with_workdir']))
srcdir = os.path.join(workdir, 'cling-src')
CLING_SRC_DIR = os.path.join(srcdir, 'tools', 'cling')
CPT_SRC_DIR = os.path.join(CLING_SRC_DIR, 'tools', 'packaging')
LLVM_OBJ_ROOT = os.path.join(workdir, 'builddir')
prefix = ''
LLVM_GIT_URL = args['with_llvm_url']
CLANG_GIT_URL = args['with_clang_url']
CLING_GIT_URL = args['with_cling_url']
EXTRA_CMAKE_FLAGS = args.get('with_cmake_flags')
CMAKE = os.environ.get('CMAKE', None)
if not CMAKE:
    if platform.system() == 'Windows':
        CMAKE = os.path.join(TMP_PREFIX, 'bin', 'cmake', 'bin', 'cmake.exe')
    else:
        CMAKE = 'cmake'

# logic is too confusing supporting both at the same time
if args.get('stdlib') and EXTRA_CMAKE_FLAGS.find('-DLLVM_ENABLE_LIBCXX=') != -1:
    print('use of --stdlib cannot be combined with -DLLVM_ENABLE_LIBCXX')
    parser.print_help()
    raise SystemExit

CLING_BRANCH = CLANG_BRANCH = LLVM_BRANCH = None
if args['current_dev']:
    cDev = args['current_dev']
    if cDev.startswith('branch:'):
      CLING_BRANCH = CLANG_BRANCH = LLVM_BRANCH = cDev[7:]
    elif cDev.startswith('branches:'):
      CLING_BRANCH, CLANG_BRANCH, LLVM_BRANCH = cDev[9:].split(',')

# llvm_revision = urlopen(
#    "https://raw.githubusercontent.com/root-project/cling/master/LastKnownGoodLLVMSVNRevision.txt").readline().strip().decode(
#   'utf-8')
VERSION = ''
REVISION = ''
# Travis needs some special behaviour
TRAVIS_BUILD_DIR = os.environ.get('TRAVIS_BUILD_DIR', None)
APPVEYOR_BUILD_FOLDER = os.environ.get('APPVEYOR_BUILD_FOLDER', None)

# Make sure git log is invoked without a pager.
os.environ['GIT_PAGER']=''

print('Cling Packaging Tool (CPT)')
print('Arguments vector: ' + str(sys.argv))
box_draw_header()
print('Thread Model: ' + FAMILY)
print('Operating System: ' + OS)
print('Distribution: ' + DIST)
print('Release: ' + RELEASE)
print('Revision: ' + REV)
print('Architecture: ' + platform.machine())
if args['compiler']:
  cInfo = None
  cInfo = exec_subprocess_check_output(args['compiler'] + ' --version', srcdir).decode('utf-8')
  print("Compiler: '%s' : %s" % (args['compiler'], cInfo.split('\n',1)[0] if cInfo else ''))

if len(sys.argv) == 1:
    print("Error: no options passed")
    parser.print_help()

# This is needed in Windows
if not os.path.isdir(workdir):
    os.makedirs(workdir)
if not (TRAVIS_BUILD_DIR or APPVEYOR_BUILD_FOLDER) and os.path.isdir(TMP_PREFIX):
    shutil.rmtree(TMP_PREFIX)
if not os.path.isdir(TMP_PREFIX):
    os.makedirs(TMP_PREFIX)

if args['check_requirements']:
    box_draw('Check availability of required softwares')
    if DIST == 'Ubuntu':
        install_line = ""
        prerequisite = ['git', 'cmake', 'gcc', 'g++', 'debhelper', 'devscripts', 'gnupg', 'python', 'SSL']
        for pkg in prerequisite:
            if check_ubuntu(pkg) is False:
                install_line += pkg + ' '
        yes = {'yes', 'y', 'ye', ''}
        no = {'no', 'n'}

        if install_line != '':
            choice = custom_input('''
    CPT will now attempt to update/install the requisite packages automatically.
    Do you want to continue? [yes/no]: ''', args['y']).lower()
            while True:
                if choice in yes:
                    # Need to communicate values to the shell. Do not use exec_subprocess_call()
                    subprocess.Popen(['sudo apt-get update'],
                                    shell=True,
                                    stdin=subprocess.PIPE,
                                    stdout=None,
                                    stderr=subprocess.STDOUT).communicate('yes'.encode('utf-8'))
                    subprocess.Popen(['sudo apt-get install ' + install_line],
                                    shell=True,
                                    stdin=subprocess.PIPE,
                                    stdout=None,
                                    stderr=subprocess.STDOUT).communicate('yes'.encode('utf-8'))
                    break
                elif choice in no:
                    print('''
    Install/update the required packages by:
    sudo apt-get update
    sudo apt-get install git cmake gcc g++ debhelper devscripts gnupg python
    ''')
                    break
                else:
                    choice = custom_input("Please respond with 'yes' or 'no': ", args['y'])
                    continue

    elif OS == 'Windows':
        check_win('git')
        check_win('python')
        check_win('SSL')
        # Check Windows registry for keys that prove an MS Visual Studio 14.0 installation
        check_win('msvc')
        print('''
Refer to the documentation of CPT for information on setting up your Windows environment.
[tools/packaging/README.md]
''')
    elif DIST == 'Fedora' or DIST == 'Scientific Linux CERN SLC':
        install_line = ''
        prerequisite = ['git', 'cmake', 'gcc', 'gcc-c++', 'rpm-build', 'python', 'SSL']
        for pkg in prerequisite:
            if check_redhat(pkg) is False:
                install_line += pkg + ' '
        yes = {'yes', 'y', 'ye', ''}
        no = {'no', 'n'}

        if install_line != '':
            choice = custom_input('''
    CPT will now attempt to update/install the requisite packages automatically.
    Do you want to continue? [yes/no]: ''', args['y']).lower()
            while True:
                if choice in yes:
                    # Need to communicate values to the shell. Do not use exec_subprocess_call()
                    subprocess.Popen(['sudo yum install ' + install_line],
                                    shell=True,
                                    stdin=subprocess.PIPE,
                                    stdout=None,
                                    stderr=subprocess.STDOUT).communicate('yes'.encode('utf-8'))
                    break
                elif choice in no:
                    print('''
    Install/update the required packages by:
    sudo yum install git cmake gcc gcc-c++ rpm-build python
    ''')
                    break
                else:
                    choice = custom_input("Please respond with 'yes' or 'no': ", args['y'])
                    continue

    if DIST == 'MacOSX':
        prerequisite = ['git', 'cmake', 'clang', 'clang++', 'python', 'SSL']
        install_line = ''
        for pkg in prerequisite:
            if check_mac(pkg) is False:
                install_line += pkg + ' '
        yes = {'yes', 'y', 'ye', ''}
        no = {'no', 'n'}

        if install_line != '':
            choice = custom_input('''
    CPT will now attempt to update/install the requisite packages automatically. Make sure you have MacPorts installed.
    Do you want to continue? [yes/no]: ''', args['y']).lower()
            while True:
                if choice in yes:
                    # Need to communicate values to the shell. Do not use exec_subprocess_call()
                    subprocess.Popen(['sudo port -v selfupdate'],
                                    shell=True,
                                    stdin=subprocess.PIPE,
                                    stdout=None,
                                    stderr=subprocess.STDOUT).communicate('yes'.encode('utf-8'))
                    subprocess.Popen(['sudo port install ' + install_line],
                                    shell=True,
                                    stdin=subprocess.PIPE,
                                    stdout=None,
                                    stderr=subprocess.STDOUT).communicate('yes'.encode('utf-8'))
                    break
                elif choice in no:
                    print('''
    Install/update the required packages by:
    sudo port -v selfupdate
    sudo port install git clang++ python
    ''')
                    break
                else:
                    choice = custom_input("Please respond with 'yes' or 'no': ", args['y'])
                    continue

if args['current_dev']:
    travis_fold_start("git-clone")
    llvm_revision = urlopen(
        "https://raw.githubusercontent.com/root-project/cling/master/LastKnownGoodLLVMSVNRevision.txt").readline().strip().decode(
        'utf-8')
    fetch_llvm(llvm_revision)
    fetch_clang(llvm_revision)
    libcpp = should_fetch_libcpp(llvm_revision)
    if libcpp: fetch_libcpp(llvm_revision, libcpp)

    # Travis has already cloned the repo out, so don;t do it again
    # Particularly important for building a pull-request
    if TRAVIS_BUILD_DIR or APPVEYOR_BUILD_FOLDER:
        ciCloned = TRAVIS_BUILD_DIR if TRAVIS_BUILD_DIR else APPVEYOR_BUILD_FOLDER
        clingDir = os.path.join(srcdir, 'tools', 'cling')
        if TRAVIS_BUILD_DIR:
            os.rename(ciCloned, clingDir)
            TRAVIS_BUILD_DIR = clingDir
        else:
            # Cannot move the directory: it is being used by another process
            os.mkdir(clingDir)
            for f in os.listdir(APPVEYOR_BUILD_FOLDER):
                shutil.move(os.path.join(APPVEYOR_BUILD_FOLDER, f), clingDir)
            APPVEYOR_BUILD_FOLDER = clingDir

        # Check validity and show some info
        box_draw("Using CI clone, last 5 commits:")
        exec_subprocess_call('git log -5 --pretty="format:%h <%ae> %<(60,trunc)%s"', clingDir)
        print('\n')
    else:
        fetch_cling(CLING_BRANCH if CLING_BRANCH else 'master')
    travis_fold_end("git-clone")

    set_version()
    if args['current_dev'] == 'tar':
        if OS == 'Windows':
            get_win_dep()
            compile(os.path.join(workdir, 'cling-win-' + platform.machine().lower() + '-' + VERSION), libcpp)
        else:
            if DIST == 'Scientific Linux CERN SLC':
                compile(os.path.join(workdir, 'cling-SLC-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
            else:
                compile(os.path.join(workdir,
                                     'cling-' + DIST + '-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        tarball()
        cleanup()

    elif args['current_dev'] == 'deb' or (args['current_dev'] == 'pkg' and DIST == 'Ubuntu'):
        compile(os.path.join(workdir, 'cling-' + VERSION), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        tarball_deb()
        debianize()
        cleanup()

    elif args['current_dev'] == 'rpm' or (args['current_dev'] == 'pkg' and platform.dist()[0] == 'redhat'):
        compile(os.path.join(workdir, 'cling-' + VERSION.replace('-' + REVISION[:7], '')), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        tarball()
        rpm_build()
        cleanup()

    elif args['current_dev'] == 'nsis' or (args['current_dev'] == 'pkg' and OS == 'Windows'):
        get_win_dep()
        compile(os.path.join(workdir, 'cling-' + RELEASE + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        make_nsi()
        build_nsis()
        cleanup()

    elif args['current_dev'] == 'dmg' or (args['current_dev'] == 'pkg' and OS == 'Darwin'):
        compile(os.path.join(workdir, 'cling-' + DIST + '-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        make_dmg()
        cleanup()

    elif args['current_dev'].startswith('branch'):
        compile(os.path.join(workdir, 'cling-' + VERSION.replace('-' + REVISION[:7], '')), libcpp)
        #install_prefix()
        if not args['no_test']:
            test_cling()
        cleanup()

    elif args['current_dev'] == 'pkg':
        compile(os.path.join(workdir, 'cling-' + DIST + '-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        tarball()
        cleanup()

if args['last_stable']:
    tag = json.loads(urlopen("https://api.github.com/repos/vgvassilev/cling/tags")
                     .read().decode('utf-8'))[0]['name'].encode('ascii', 'ignore').decode("utf-8")

    # For Python 3 compatibility
    tag = str(tag)

    # FIXME
    assert tag[0] is "v"
    assert CLING_BRANCH == None
    llvm_revision = urlopen(
        'https://raw.githubusercontent.com/root-project/cling/%s/LastKnownGoodLLVMSVNRevision.txt' % tag
    ).readline().strip().decode('utf-8')

    fetch_llvm(llvm_revision)
    fetch_clang(llvm_revision)
    libcpp = should_fetch_libcpp(llvm_revision)
    if libcpp: fetch_libcpp(llvm_revision, libcpp)

    print("Last stable Cling release detected: ", tag)
    fetch_cling(tag)

    if args['last_stable'] == 'tar':
        set_version()
        if OS == 'Windows':
            get_win_dep()
            compile(os.path.join(workdir, 'cling-win-' + platform.machine().lower() + '-' + VERSION), libcpp)
        else:
            if DIST == 'Scientific Linux CERN SLC':
                compile(os.path.join(workdir, 'cling-SLC-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
            else:
                compile(os.path.join(workdir,
                                     'cling-' + DIST + '-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        tarball()
        cleanup()

    elif args['last_stable'] == 'deb' or (args['last_stable'] == 'pkg' and DIST == 'Ubuntu'):
        set_version()
        compile(os.path.join(workdir, 'cling-' + VERSION), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        tarball_deb()
        debianize()
        cleanup()

    elif args['last_stable'] == 'rpm' or (args['last_stable'] == 'pkg' and platform.dist()[0] == 'redhat'):
        set_version()
        compile(os.path.join(workdir, 'cling-' + VERSION), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        tarball()
        rpm_build()
        cleanup()

    elif args['last_stable'] == 'nsis' or (args['last_stable'] == 'pkg' and OS == 'Windows'):
        set_version()
        get_win_dep()
        compile(os.path.join(workdir, 'cling-' + DIST + '-' + REV + '-' + platform.machine() + '-' + VERSION), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        make_nsi()
        build_nsis()
        cleanup()

    elif args['last_stable'] == 'dmg' or (args['last_stable'] == 'pkg' and OS == 'Darwin'):
        set_version()
        compile(os.path.join(workdir, 'cling-' + DIST + '-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        make_dmg()
        cleanup()

    elif args['last_stable'] == 'pkg':
        set_version()
        compile(os.path.join(workdir, 'cling-' + DIST + '-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
        install_prefix()
        if not args['no_test']:
            test_cling()
        tarball()
        cleanup()

if args['tarball_tag']:
    llvm_revision = urlopen(
        "https://raw.githubusercontent.com/root-project/cling/%s/LastKnownGoodLLVMSVNRevision.txt" % args[
            'tarball_tag']).readline().strip().decode(
        'utf-8')
    fetch_llvm(llvm_revision)
    fetch_clang(llvm_revision)
    fetch_cling(args['tarball_tag'])
    libcpp = should_fetch_libcpp(llvm_revision)
    if libcpp: fetch_libcpp(llvm_revision, libcpp)

    set_version()

    if OS == 'Windows':
        get_win_dep()
        compile(os.path.join(workdir, 'cling-win-' + platform.machine().lower() + '-' + VERSION))
    else:
        if DIST == 'Scientific Linux CERN SLC':
            compile(os.path.join(workdir, 'cling-SLC-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
        else:
            compile(
                os.path.join(workdir, 'cling-' + DIST + '-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)

    install_prefix()
    if not args['no_test']:
        test_cling()
    tarball()
    cleanup()

if args['deb_tag']:
    llvm_revision = urlopen(
        "https://raw.githubusercontent.com/root-project/cling/%s/LastKnownGoodLLVMSVNRevision.txt" % args[
            'deb_tag']).readline().strip().decode(
        'utf-8')
    fetch_llvm(llvm_revision)
    fetch_clang(llvm_revision)
    fetch_cling(args['deb_tag'])
    libcpp = should_fetch_libcpp(llvm_revision)
    if libcpp: fetch_libcpp(llvm_revision, libcpp)

    set_version()
    compile(os.path.join(workdir, 'cling-' + VERSION), libcpp)
    install_prefix()
    if not args['no_test']:
        test_cling()
    tarball_deb()
    debianize()
    cleanup()

if args['rpm_tag']:
    llvm_revision = urlopen(
        "https://raw.githubusercontent.com/root-project/cling/%s/LastKnownGoodLLVMSVNRevision.txt" % args[
            'rpm_tag']).readline().strip().decode(
        'utf-8')
    fetch_llvm(llvm_revision)
    fetch_clang(llvm_revision)
    fetch_cling(args['rpm_tag'])
    libcpp = should_fetch_libcpp(llvm_revision)
    if libcpp: fetch_libcpp(llvm_revision, libcpp)

    set_version()
    compile(os.path.join(workdir, 'cling-' + VERSION), libcpp)
    install_prefix()
    if not args['no_test']:
        test_cling()
    tarball()
    rpm_build()
    cleanup()

if args['nsis_tag']:
    llvm_revision = urlopen(
        "https://raw.githubusercontent.com/root-project/cling/%s/LastKnownGoodLLVMSVNRevision.txt" % args[
            'nsis_tag']).readline().strip().decode(
        'utf-8')
    fetch_llvm(llvm_revision)
    libcpp = fetch_libcpp(llvm_revision)
    fetch_clang(llvm_revision)
    fetch_cling(args['nsis_tag'])
    set_version()
    get_win_dep()
    compile(os.path.join(workdir, 'cling-' + DIST + '-' + REV + '-' + platform.machine() + '-' + VERSION), libcpp)
    install_prefix()
    if not args['no_test']:
        test_cling()
    make_nsi()
    build_nsis()
    cleanup()

if args['dmg_tag']:
    llvm_revision = urlopen(
        "https://raw.githubusercontent.com/root-project/cling/%s/LastKnownGoodLLVMSVNRevision.txt" % args[
            'dmg_tag']).readline().strip().decode(
        'utf-8')
    fetch_llvm(llvm_revision)
    fetch_clang(llvm_revision)
    fetch_cling(args['dmg_tag'])
    libcpp = should_fetch_libcpp(llvm_revision)
    if libcpp: fetch_libcpp(llvm_revision, libcpp)

    set_version()
    compile(os.path.join(workdir, 'cling-' + DIST + '-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
    install_prefix()
    if not args['no_test']:
        test_cling()
    make_dmg()
    cleanup()

if args['create_dev_env']:
    llvm_revision = urlopen(
        "https://raw.githubusercontent.com/root-project/cling/master/LastKnownGoodLLVMSVNRevision.txt"
    ).readline().strip().decode('utf-8')
    fetch_llvm(llvm_revision)
    fetch_clang(llvm_revision)
    fetch_cling('master')
    libcpp = should_fetch_libcpp(llvm_revision)
    if libcpp: fetch_libcpp(llvm_revision, libcpp)

    set_version()
    if OS == 'Windows':
        get_win_dep()
        compile(os.path.join(workdir, 'cling-win-' + platform.machine().lower() + '-' + VERSION), libcpp)
    else:
        if DIST == 'Scientific Linux CERN SLC':
            compile(os.path.join(workdir, 'cling-SLC-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
        else:
            compile(
                os.path.join(workdir, 'cling-' + DIST + '-' + REV + '-' + platform.machine().lower() + '-' + VERSION), libcpp)
    install_prefix()
    if not args['no_test']:
        test_cling()

if args['make_proper']:
    # This is an internal option in CPT, meant to be integrated into Cling's build system.
    with open(os.path.join(LLVM_OBJ_ROOT, 'config.log'), 'r') as log:
        for line in log:
            if re.match('^LLVM_PREFIX=', line):
                prefix = re.sub('^LLVM_PREFIX=', '', line).replace("'", '').strip()

    set_version()
    install_prefix()
    cleanup()
