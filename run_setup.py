#!/usr/bin/env python
'''
*******************************************************
 Copyright (c) MLRS project
 GPL3
 https://www.gnu.org/licenses/gpl-3.0.de.html
 OlliW @ www.olliw.eu
*******************************************************
 run_setup.py
 1. run git submodule update --init --recursive
 2. calls run_copy_st_drivers.py to populate the target ST Driver folders
 3. calls fmav_generate_c_library.py to generate MAVLink library files
 4. calls dronecan_generate_c_library.py to generate DroneCAN library files
 version 25.01.2026
********************************************************
'''
import os
import subprocess
import sys


mLRSProjectdirectory = os.path.dirname(os.path.abspath(__file__))
mLRSdirectory = os.path.join(mLRSProjectdirectory,'mLRS')

python_cmd = sys.executable
silent = False
pause_on_exit = False


def wait_for_enter():
    if pause_on_exit:
        print('Press Enter to continue')
        input()


def os_system(arg):
    if silent:
        res = subprocess.call(arg, stdout=subprocess.DEVNULL)
    else:
        res = subprocess.call(arg)
    if res != 0:
        print('# ERROR (errno =',res,') DONE #', file=sys.stderr)
        wait_for_enter()
        sys.exit(1)


def check_python():
    if sys.version_info.major != 3:
        print("ERROR: Python 3 not found on your system. Please make sure Python 3 is available.")
        wait_for_enter()
        sys.exit(1)


def git_submodules_update():
    print('----------------------------------------')
    print(' run git submodule update --init --recursive')
    print('----------------------------------------')
    os_system(['git', 'submodule', 'update', '--init', '--recursive'])
    print('# DONE #')


def copy_st_drivers():
    print('----------------------------------------')
    print(' run run_copy_st_drivers.py')
    print('----------------------------------------')
    os.chdir(os.path.join(mLRSProjectdirectory,'tools'))
    os_system([python_cmd, os.path.join('.' , 'run_copy_st_drivers.py'), '-silent'])
    print('# DONE #')


def generate_mavlink_c_library():
    print('----------------------------------------')
    print(' run fmav_generate_c_library.py')
    print('----------------------------------------')
    os.chdir(os.path.join(mLRSdirectory,'Common','mavlink'))
    os_system([python_cmd, os.path.join('.','fmav_generate_c_library.py')])
    print('# DONE #')


def generate_dronecan_c_library():
    print('----------------------------------------')
    print(' run dronecan_generate_c_library.py')
    print('----------------------------------------')
    os.chdir(os.path.join(mLRSdirectory,'Common','dronecan'))
    os_system([python_cmd, os.path.join('.','dronecan_generate_c_library.py'), '-np'])
    print('# DONE #')


def main(argv=None):
    global pause_on_exit
    global silent

    if argv is None:
        argv = sys.argv[1:]

    silent = '--silent' in argv or '--s' in argv
    pause_on_exit = sys.stdin.isatty() and '--no-pause' not in argv and '-np' not in argv

    cmdline_submodules_update = False
    cmdline_copy_st_drivers = False
    cmdline_mavlink = False
    cmdline_dronecan = False
    hascmd = False

    for cmd in argv:
        if cmd == '--submodules' or cmd == '-g' or cmd == '-G':
            cmdline_submodules_update = True
            hascmd = True
        if cmd == '--copy' or cmd == '-c' or cmd == '-C':
            cmdline_copy_st_drivers = True
            hascmd = True
        if cmd == '--mavlink' or cmd == '-m' or cmd == '-M':
            cmdline_mavlink = True
            hascmd = True
        if cmd == '--dronecan' or cmd == '-d' or cmd == '-D':
            cmdline_dronecan = True
            hascmd = True

    check_python()
    if cmdline_submodules_update or not hascmd:
        git_submodules_update()
    if cmdline_copy_st_drivers or not hascmd:
        copy_st_drivers()
    if cmdline_mavlink or not hascmd:
        generate_mavlink_c_library()
    if cmdline_dronecan or not hascmd:
        generate_dronecan_c_library()

    wait_for_enter()
    return 0


if __name__ == "__main__":
    sys.exit(main())
