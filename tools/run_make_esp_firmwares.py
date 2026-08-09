#!/usr/bin/env python3
'''
*******************************************************
 Copyright (c) MLRS project
 GPL3
 https://www.gnu.org/licenses/gpl-3.0.de.html
 OlliW @ www.olliw.eu
*******************************************************
 run_make_esp_firmwares.py
 build ESP firmware files and publish verified binaries
********************************************************
'''

import argparse
import configparser
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


MLRS_PROJECT_DIR = Path(__file__).resolve().parent.parent
MLRS_PIO_BUILD_DIR = MLRS_PROJECT_DIR / '.pio' / 'build'
MLRS_ESP_BUILD_DIR = MLRS_PROJECT_DIR / 'tools' / 'esp-build'


class UsageError(ValueError):
    pass


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(
        description='Build ESP PlatformIO environments and publish verified binaries.',
    )
    parser.add_argument(
        '--target', '-t', '-T',
        help='build one exact PlatformIO environment (default: all environments)',
    )
    parser.add_argument(
        '--define', '-d', '-D', action='append', default=[], metavar='NAME[=VALUE]',
        help='append a preprocessor definition; may be repeated',
    )
    parser.add_argument(
        '--platformio', metavar='PATH',
        help='path to pio/platformio executable or its containing directory',
    )
    parser.add_argument(
        '--version', '-v', '-V',
        help='override firmware version used in published filenames',
    )
    parser.add_argument(
        '--nopause', '-np', action='store_true',
        help='do not wait for Enter after a Windows run',
    )
    return parser.parse_args(argv)


def read_version(project_dir):
    common_conf = project_dir / 'mLRS' / 'Common' / 'common_conf.h'
    content = common_conf.read_text(encoding='utf-8')
    match = re.search(r'VERSIONONLYSTR\s+"(\S+)"', content)
    if not match:
        raise RuntimeError('VERSIONONLYSTR not found in %s' % common_conf)
    return match.group(1)


def git_output(project_dir, *args, required=True):
    result = subprocess.run(
        ['git', *args], cwd=str(project_dir), capture_output=True, text=True,
        check=False,
    )
    if result.returncode != 0:
        if required:
            raise RuntimeError(
                'git %s failed with exit code %d' % (' '.join(args), result.returncode)
            )
        return ''
    return result.stdout.strip()


def version_suffix(project_dir, version):
    try:
        patch_component = version.split('.')[2]
    except IndexError:
        raise UsageError('version must contain a numeric patch component: %s' % version)
    patch_match = re.match(r'(\d+)', patch_component)
    if not patch_match:
        raise UsageError('version must contain a numeric patch component: %s' % version)
    patch = int(patch_match.group(1))

    branch = git_output(project_dir, 'branch', '--show-current', required=False)
    branch_suffix = ''
    if branch and branch != 'main' and patch != 0:
        safe_branch = re.sub(r'[^A-Za-z0-9._-]+', '-', branch).strip('-')
        if safe_branch:
            branch_suffix = '-' + safe_branch

    hash_suffix = ''
    if patch % 2 == 1:
        commit_hash = git_output(project_dir, 'rev-parse', '--short', 'HEAD')
        hash_suffix = '-@' + commit_hash

    return branch_suffix + hash_suffix


def platformio_environments(project_dir):
    config_path = project_dir / 'platformio.ini'
    parser = configparser.ConfigParser(interpolation=None)
    with config_path.open(encoding='utf-8') as config_file:
        parser.read_file(config_file)
    environments = [
        section[4:] for section in parser.sections() if section.startswith('env:')
    ]
    if not environments:
        raise RuntimeError('no PlatformIO environments found in %s' % config_path)
    return environments


def resolve_platformio(explicit=None):
    candidates = []
    if explicit:
        explicit_path = Path(explicit).expanduser()
        if explicit_path.is_dir():
            candidates.extend(
                str(explicit_path / name)
                for name in ('pio', 'platformio', 'pio.exe', 'platformio.exe')
            )
        else:
            candidates.append(str(explicit_path))
    else:
        candidates.extend(('pio', 'platformio'))

    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return resolved

    if explicit:
        raise FileNotFoundError('PlatformIO executable not found at %s' % explicit)
    raise FileNotFoundError(
        'PlatformIO executable not found in PATH; use --platformio PATH'
    )


def build_environment(defines):
    environment = os.environ.copy()
    extra_flags = '\n'.join('-D%s' % define for define in defines)
    if extra_flags:
        current_flags = environment.get('PLATFORMIO_BUILD_FLAGS', '')
        environment['PLATFORMIO_BUILD_FLAGS'] = '\n'.join(
            flags for flags in (current_flags, extra_flags) if flags
        )
    return environment


def run_checked(command, description, environment):
    print(description, flush=True)
    try:
        result = subprocess.run(command, env=environment, check=True)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(
            '%s failed with exit code %d' % (description, error.returncode)
        )
    # Keep the explicit check for test doubles and non-standard subprocess wrappers.
    if result.returncode != 0:
        raise RuntimeError(
            '%s failed with exit code %d' % (description, result.returncode)
        )


def remove_build_directories(build_dir, environments):
    for environment in environments:
        environment_dir = build_dir / environment
        if environment_dir.is_symlink() or environment_dir.is_file():
            environment_dir.unlink()
        elif environment_dir.exists():
            shutil.rmtree(environment_dir)


def compile_environments(platformio, project_dir, build_dir, environments, target, defines):
    command = [platformio, 'run', '--project-dir', str(project_dir)]
    if target:
        command.extend(['--environment', target])

    environment = build_environment(defines)
    run_checked(command + ['--target', 'fullclean'], 'PlatformIO clean', environment)

    # Do not let a broken/no-op clean command leave stale firmware.bin files that
    # could be mistaken for products of the following build.
    remove_build_directories(build_dir, environments)
    run_checked(command, 'PlatformIO build', environment)


def validate_artifacts(build_dir, environments):
    artifacts = []
    for environment in environments:
        artifact = build_dir / environment / 'firmware.bin'
        if not artifact.is_file():
            raise RuntimeError('expected artifact was not produced: %s' % artifact)
        if artifact.stat().st_size <= 0:
            raise RuntimeError('expected artifact is empty: %s' % artifact)
        artifacts.append((environment, artifact))
    return artifacts


def remove_path(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        shutil.rmtree(path)


def publish_artifacts(artifacts, output_dir, version, suffix):
    output_dir.mkdir(parents=True, exist_ok=True)
    staging_dir = Path(tempfile.mkdtemp(prefix='.firmware.', dir=str(output_dir)))
    destination = output_dir / 'firmware'
    try:
        for environment, artifact in artifacts:
            filename = '%s-%s%s.bin' % (environment, version, suffix)
            print('%s -> %s' % (environment, filename))
            shutil.copy2(artifact, staging_dir / filename)

        remove_path(destination)
        staging_dir.replace(destination)
    except Exception:
        remove_path(staging_dir)
        raise
    return destination


def execute(args, project_dir=MLRS_PROJECT_DIR, build_dir=MLRS_PIO_BUILD_DIR,
            output_dir=MLRS_ESP_BUILD_DIR):
    project_dir = Path(project_dir)
    build_dir = Path(build_dir)
    output_dir = Path(output_dir)

    all_environments = platformio_environments(project_dir)
    selected_environment = next(
        (
            environment for environment in all_environments
            if environment.lower() == args.target.lower()
        ),
        None,
    ) if args.target else None
    if args.target and selected_environment is None:
        raise UsageError('unknown PlatformIO environment: %s' % args.target)
    environments = [selected_environment] if selected_environment else all_environments

    platformio = resolve_platformio(args.platformio)
    version = args.version or read_version(project_dir)
    suffix = version_suffix(project_dir, version)

    print('VERSIONONLYSTR =', version, flush=True)
    if suffix:
        print('filename suffix =', suffix, flush=True)
    print('PlatformIO =', platformio, flush=True)
    print('environments =', len(environments), flush=True)

    compile_environments(
        platformio, project_dir, build_dir, environments, selected_environment, args.define,
    )
    artifacts = validate_artifacts(build_dir, environments)
    destination = publish_artifacts(artifacts, output_dir, version, suffix)
    print('published %d verified binaries to %s' % (len(artifacts), destination))


def main(argv=None):
    args = parse_arguments(argv)
    try:
        execute(args)
    except UsageError as error:
        print('ERROR:', error, file=sys.stderr)
        return 2
    except (OSError, RuntimeError) as error:
        print('ERROR:', error, file=sys.stderr)
        return 1

    if os.name == 'nt' and not args.nopause:
        input('Press Enter to continue...')
    return 0


if __name__ == '__main__':
    sys.exit(main())
