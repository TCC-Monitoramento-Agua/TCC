"""Inclui bootloader e partições na imagem Wokwi, sem incluir/resetar a fila."""
Import('env')


def gerar(source, target, env):
    import os
    import subprocess
    platform = env.PioPlatform()
    # Plataformas recentes usam um executável; as anteriores fornecem esptool.py.
    if 'tool-esptool' in platform.packages:
        executable = [os.path.join(platform.get_package_dir('tool-esptool'), 'esptool')]
    else:
        tool = os.path.join(platform.get_package_dir('tool-esptoolpy'), 'esptool.py')
        executable = [env.subst('$PYTHONEXE'), tool]
    framework = platform.get_package_dir('framework-arduinoespressif32')
    build = env.subst('$BUILD_DIR')
    command = executable + ['--chip', 'esp32', 'merge_bin',
               '-o', os.path.join(build, 'firmware-merged.bin'),
               '--flash_mode', 'dio', '--flash_freq', '40m', '--flash_size', '4MB',
               '0x1000', os.path.join(build, 'bootloader.bin'),
               '0x8000', os.path.join(build, 'partitions.bin'),
               '0xe000', os.path.join(framework, 'tools', 'partitions', 'boot_app0.bin'),
               '0x10000', os.path.join(build, 'firmware.bin')]
    subprocess.run(command, check=True)


env.AddPostAction('$BUILD_DIR/${PROGNAME}.bin', gerar)
